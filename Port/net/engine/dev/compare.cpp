// Port/net engine dev (docs/netcode.md, "Checks"): the comparison check (GTA2_NET_COMPARE: the client's prediction against
// each snapshot, field by field), the divergence check (GTA2_NET_DIVERGE), and the ped and rand() dumps that go with them.
#include "../internal.hpp"
#include "dev.hpp"
#include <chrono>
#include "Source/Camera.hpp"
#include "Source/Car.hpp"
#include "Source/Draw.hpp"
#include "Source/Frontend.hpp"
#include "Source/gtx_0x106C.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/ObjectDefinitions.hpp"
#include "Source/Object_5C.hpp"
#include "Source/ObjectPool.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/SoundObject.hpp"
#include "Source/SpriteGrid.hpp"
#include "Source/Weapon.hpp"
#include "Source/char.hpp"
#include "Source/collide.hpp"
#include "Source/gbh_graphics.hpp"
#include "Source/sound_obj.hpp"
#include "Source/sprite.hpp"
#include <math.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#include "Port/rand/state.hpp"
#include "Source/rng.hpp"
#include "Source/PedGroup.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <set>
#include <string.h>
#include <string>
#include <unordered_map>

namespace
{
using namespace net::engine;
using net::EntityState;
using net::Snapshot;
} // namespace

// --- The comparison check (docs/netcode.md, "Checks"): the client's prediction for a tick against the server's state
// at it, field by field. Called with the game restored to the snapshot's tick, before the snapshot is applied ---

namespace
{
struct FieldTally
{
    uint64_t compared = 0, differ = 0;
    double absSum = 0;
};
std::vector<std::vector<FieldTally>> gTally; // [type][field]
uint64_t gMissing[8]; // per type: in the snapshot, mapped, but not captured here
uint64_t gAsleepHere, gAsleepThere; // cars with physics on one side only (asleep on the other)

bool IsCop(Ped* p)
{
    const s32 o = p->field_240_occupation;
    return (o >= ped_ocupation_enum::police && o <= ped_ocupation_enum::walking_army_guard_31) || o == ped_ocupation_enum::roadblock_cop_37;
}

// A car reference of this game's (CarRef) as the server's
int32_t ServerCarRef(int32_t ref, const std::unordered_map<int32_t, int32_t>& toServer)
{
    auto it = toServer.find(ref);
    return it != toServer.end() ? it->second : ref ? -1 : 0;
}
} // namespace

namespace
{
// Each entity of the snapshot this game has too, with this game's state of it as captured here (its references
// into the server's ids): fn(server's, here's, a cop)
template <class Fn> void ForEachPair(const Snapshot& snap, Fn fn)
{
    using namespace net::engine;
    Snapshot here;
    Capture(here);
    std::unordered_map<uint32_t, const EntityState*> byId;
    for (const EntityState& e : here.entities)
    {
        byId[e.id] = &e;
    }
    std::unordered_map<int32_t, int32_t> carToServer; // this game's CarRef -> the server's
    std::unordered_map<int32_t, int32_t> carIdToServer, pedIdToServer; // this game's net ids -> the server's
    for (const auto& kv : ReplicaCars())
    {
        carToServer[CarRef(kv.second)] = (int32_t)(kv.first - kCarIdBase) + 1;
        carIdToServer[(int32_t)(kCarIdBase + kv.second->field_6C_car_id)] = (int32_t)kv.first;
    }
    for (const auto& kv : ReplicaPeds())
    {
        pedIdToServer[(int32_t)(kPedIdBase + kv.second->field_200_id)] = (int32_t)kv.first;
    }
    static std::vector<int> pedRefs, carRefs;
    if (pedRefs.empty())
    {
        PedStateRefFields(&pedRefs, &carRefs);
    }
    const auto toServer = [](int32_t id, const std::unordered_map<int32_t, int32_t>& map) {
        auto it = map.find(id);
        return id < (int32_t)kPedIdBase && id < (int32_t)kCarIdBase ? id : it != map.end() ? it->second : -1; // players: the same
    };
    for (const EntityState& s : snap.entities)
    {
        uint32_t local = s.id; // the world and players: the same ids
        bool cop = false;
        if (s.type == kEntityCar)
        {
            Car* c = ReplicaCar(s.id);
            local = c ? kCarIdBase + (uint32_t)c->field_6C_car_id : 0;
        }
        else if (s.type == kEntityPed)
        {
            Ped* p = ReplicaPed(s.id);
            local = p ? kPedIdBase + p->field_200_id : 0;
            cop = p && IsCop(p);
        }
        else if (s.type == kEntityObject)
        {
            continue; // Pickups are matched by place (pickups.cpp), not compared here
        }
        if (EntityAge(s))
        {
            continue; // an older state of it, sent again (a busy area's budget): not this tick's, and not applied
        }
        if (!local && s.type != kEntityWorld)
        {
            continue; // not mapped (yet): nothing to compare
        }
        auto it = byId.find(local);
        if (it == byId.end())
        {
            gMissing[s.type]++;
            continue;
        }
        EntityState h = *it->second;
        if (s.type == kEntityCar && CarHasPhysics(h) != CarHasPhysics(s))
        {
            (CarHasPhysics(s) ? gAsleepHere : gAsleepThere)++;
        }
        // References: this game's ids into the server's
        if (s.type == kEntityPlayer)
        {
            h.f[kPlayerCar] = ServerCarRef(h.f[kPlayerCar], carToServer);
            h.f[kPlayerTarget] = ServerCarRef(h.f[kPlayerTarget], carToServer);
        }
        else if (s.type == kEntityPed)
        {
            h.f[kPedCar] = ServerCarRef(h.f[kPedCar], carToServer);
            for (int f : pedRefs)
            {
                h.f[f] = h.f[f] ? toServer(h.f[f], pedIdToServer) : 0;
            }
            for (int f : carRefs)
            {
                h.f[f] = h.f[f] ? toServer(h.f[f], carIdToServer) : 0;
            }
        }
        fn(s, h, cop);
    }
}

void EnsureTally(std::vector<std::vector<FieldTally>>& tally)
{
    const net::Schema& schema = net::engine::NetSchema();
    if (tally.empty())
    {
        tally.resize(schema.types.size() + 1); // the last: cops, apart from the other peds
        for (size_t t = 0; t < schema.types.size(); t++)
        {
            tally[t].resize(schema.types[t].fields.size());
        }
        tally.back().resize(schema.types[net::engine::kEntityPed].fields.size());
    }
}
} // namespace

void net::engine::ComparePrediction(const Snapshot& snap)
{
    EnsureTally(gTally);
    ForEachPair(snap, [](const EntityState& s, const EntityState& h, bool cop) {
        std::vector<FieldTally>& tally = cop ? gTally.back() : gTally[s.type];
        for (size_t f = 0; f < tally.size(); f++)
        {
            // Not the client's to predict: a player's deaths (a client counts its own, only to carry out each once),
            // its points (set from the snapshot after this comparison), the world's reach (the server's area)
            if ((s.type == kEntityPlayer && (f == kPlayerDeaths || f == kPlayerPoints)) || (s.type == kEntityWorld && f == kWorldReach))
            {
                continue;
            }
            tally[f].compared++;
            if (h.f[f] != s.f[f])
            {
                tally[f].differ++;
                tally[f].absSum += fabs((double)h.f[f] - (double)s.f[f]);
            }
        }
    });
}

// --- The divergence check (GTA2_NET_DIVERGE, docs/netcode.md "Checks"): the server's state applied once, then this
// game on its own. Per field, after how many ticks each entity first differed from the server's ---

namespace
{
const int kAgeBuckets = 9; // 0 (right after the apply), 1, 2-3, 4-7, ... 64-127, 128+
struct Divergence
{
    uint64_t first[kAgeBuckets] = {};
    bool seen = false;
    uint32_t exampleId = 0, exampleAge = 0;
    int32_t exampleThere = 0, exampleHere = 0;
};
std::vector<std::vector<Divergence>> gDiverged; // [type][field]
std::unordered_map<uint64_t, bool> gDivergedOnce; // (entity id, field): differed already
std::unordered_map<uint32_t, int> gComparedEntities; // per type, the entities compared at least once
uint64_t gDivergeMissing[8];

int AgeBucket(uint32_t age)
{
    int b = 0;
    while (b < kAgeBuckets - 1 && age >= (1u << b))
    {
        b++;
    }
    return b; // 0 -> 0, 1 -> 1, 2-3 -> 2, ...
}
} // namespace

void net::engine::CompareDivergence(const Snapshot& snap, uint32_t age)
{
    const Schema& schema = NetSchema();
    if (gDiverged.empty())
    {
        gDiverged.resize(schema.types.size() + 1);
        for (size_t t = 0; t < schema.types.size(); t++)
        {
            gDiverged[t].resize(schema.types[t].fields.size());
        }
        gDiverged.back().resize(schema.types[kEntityPed].fields.size());
    }
    const uint64_t missingBefore = gMissing[kEntityCar] + gMissing[kEntityPed];
    ForEachPair(snap, [&](const EntityState& s, const EntityState& h, bool cop) {
        const size_t t = cop ? gDiverged.size() - 1 : s.type;
        gComparedEntities[(uint32_t)t * 0x10000000u + (s.id & 0x0FFFFFFF)] = 1;
        std::vector<Divergence>& d = gDiverged[t];
        for (size_t f = 0; f < d.size(); f++)
        {
            const uint64_t key = ((uint64_t)s.id << 16) | f;
            if (h.f[f] != s.f[f] && !gDivergedOnce.count(key))
            {
                gDivergedOnce[key] = true;
                Divergence& x = d[f];
                if (!x.seen || age < x.exampleAge)
                {
                    x.seen = true, x.exampleId = s.id, x.exampleAge = age, x.exampleThere = s.f[f], x.exampleHere = h.f[f];
                }
                x.first[AgeBucket(age)]++;
            }
        }
    });
    gDivergeMissing[kEntityCar] += gMissing[kEntityCar] + gMissing[kEntityPed] - missingBefore; // Cars and peds together
    gMissing[kEntityCar] = gMissing[kEntityPed] = 0;
}

void net::engine::DumpPeds(uint32_t tick, bool server)
{
    static FILE* f = NULL;
    static uint32_t from = 0, to = 0xFFFFFFFF;
    static bool opened = false;
    if (!opened)
    {
        opened = true;
        if (const char* spec = getenv("GTA2_NET_DUMPPEDS"))
        {
            std::string path = spec;
            const size_t colon = path.find(':');
            if (colon != std::string::npos)
            {
                sscanf(path.c_str() + colon + 1, "%u:%u", &from, &to);
                path.resize(colon);
            }
            f = fopen(path.c_str(), "wb");
        }
    }
    if (!f || tick < from || tick > to)
    {
        return;
    }
    std::unordered_map<const Ped*, uint32_t> toServer;
    if (!server)
    {
        for (const auto& kv : ReplicaPeds())
        {
            toServer[kv.second] = kv.first;
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        uint32_t id = p->field_15C_player ? 1 + p->field_15C_player->field_2E_player_idx : server ? kPedIdBase + p->field_200_id : 0;
        if (!id)
        {
            auto it = toServer.find(p);
            id = it != toServer.end() ? it->second : 0;
        }
        if (!id)
        {
            continue; // one of this game's own
        }
        const Char* c = p->field_168_game_object;
        const Sprite* s = c ? c->field_80_sprite_ptr : NULL;
        const uint32_t head[5] = {tick, id, (uint32_t)sizeof(Ped), c ? (uint32_t)sizeof(Char) : 0, s ? (uint32_t)sizeof(Sprite) : 0};
        fwrite(head, sizeof(head), 1, f);
        fwrite(p, sizeof(Ped), 1, f);
        if (c)
        {
            fwrite(c, sizeof(Char), 1, f);
        }
        if (s)
        {
            fwrite(s, sizeof(Sprite), 1, f);
        }
    }
    fflush(f);
}

namespace
{
FILE* gRandLog;
uint32_t gRandFrom, gRandTo;
void LogRand(void* caller)
{
    const uint32_t cycle = (uint32_t)gpRng_67AB34->get_cur_rng_41CFE0();
    if (cycle >= gRandFrom && cycle <= gRandTo)
    {
        uintptr_t base = 0;
#if !defined(_WIN32)
        Dl_info info;
        base = dladdr(caller, &info) ? (uintptr_t)info.dli_fbase : 0;
#endif
        // the caller, and two above it (past the rng wrappers: rng::get_int_4F7AE0 and the like)
        fprintf(gRandLog, "%u 0x%lx 0x%lx 0x%lx\n", cycle, (unsigned long)((uintptr_t)caller - base),
                (unsigned long)((uintptr_t)__builtin_return_address(2) - base), (unsigned long)((uintptr_t)__builtin_return_address(3) - base));
    }
}
} // namespace

void net::engine::StartRandLog()
{
    const char* spec = getenv("GTA2_NET_RANDLOG");
    if (!spec || gRandLog)
    {
        return;
    }
    std::string path = spec;
    const size_t colon = path.find(':');
    if (colon != std::string::npos)
    {
        sscanf(path.c_str() + colon + 1, "%u:%u", &gRandFrom, &gRandTo);
        path.resize(colon);
    }
    gRandLog = fopen(path.c_str(), "w");
    gPortRandLog = gRandLog ? LogRand : NULL;
}

void net::engine::PrintDivergence()
{
    const Schema& schema = NetSchema();
    int counts[16] = {};
    for (const auto& kv : gComparedEntities)
    {
        counts[kv.first / 0x10000000u]++;
    }
    printf("net: divergence, entities compared:");
    for (size_t t = 0; t < gDiverged.size(); t++)
    {
        printf(" %s %d", t == schema.types.size() ? "cop" : schema.types[t].name, counts[t]);
    }
    printf("; mapped but gone here %llu times\n", (unsigned long long)gDivergeMissing[kEntityCar]);
    printf("net: divergence, per field the entities that first differed after 0 | 1 | 2-3 | 4-7 | 8-15 | 16-31 | 32-63 | 64-127 | 128+ ticks:\n");
    for (size_t t = 0; t < gDiverged.size(); t++)
    {
        const bool cop = t == schema.types.size();
        for (size_t f = 0; f < gDiverged[t].size(); f++)
        {
            const Divergence& x = gDiverged[t][f];
            if (!x.seen)
            {
                continue;
            }
            printf("  %s.%s:", cop ? "cop" : schema.types[t].name, schema.types[cop ? kEntityPed : t].fields[f].name);
            for (int b = 0; b < kAgeBuckets; b++)
            {
                printf(" %llu", (unsigned long long)x.first[b]);
            }
            printf("  (first: entity %u after %u ticks, server %d, here %d)\n", x.exampleId, x.exampleAge, x.exampleThere, x.exampleHere);
        }
    }
    fflush(stdout);
}

void net::engine::PrintPredictionStats(uint32_t frame)
{
    const Schema& schema = NetSchema();
    struct Row
    {
        double share;
        std::string line;
    };
    std::vector<Row> rows;
    for (size_t t = 0; t < gTally.size(); t++)
    {
        for (size_t f = 0; f < gTally[t].size(); f++)
        {
            const FieldTally& x = gTally[t][f];
            if (x.differ)
            {
                char line[160];
                const bool cop = t == schema.types.size();
                snprintf(line, sizeof(line), "%s.%s %.1f%% (%llu of %llu, %.1f off on average)", cop ? "cop" : schema.types[t].name,
                         schema.types[cop ? kEntityPed : t].fields[f].name,
                         100.0 * x.differ / x.compared, (unsigned long long)x.differ, (unsigned long long)x.compared, x.absSum / x.differ);
                rows.push_back(Row{(double)x.differ / x.compared, line});
            }
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.share > b.share; });
    printf("net: frame %u, prediction against the server, the fields that differ most:", frame);
    for (size_t i = 0; i < rows.size() && i < 12; i++)
    {
        printf("%s %s", i ? ";" : "", rows[i].line.c_str());
    }
    printf("%s; not captured here: %llu cars, %llu peds; car physics asleep here only %llu, there only %llu\n", rows.empty() ? " none" : "",
           (unsigned long long)gMissing[kEntityCar], (unsigned long long)gMissing[kEntityPed], (unsigned long long)gAsleepHere, (unsigned long long)gAsleepThere);
    gTally.clear();
    memset(gMissing, 0, sizeof(gMissing));
    gAsleepHere = gAsleepThere = 0;
}

