// Port (docs/netcode.md, N0): GTA2_SIMBENCH=<flip>[:<ticks>] measures the simulation for client-side prediction.
// At that flip (or GTA2_SIMBENCH=p<n>: once n peds are alive; in a game) it runs <ticks> (default 3000) sim ticks back to back, without drawing or audio, and
// prints to stderr:
//   - the time per tick of each subsystem service in Game::UpdateGame_4B9410 (the same calls in the same order);
//   - the size and save/restore time of the full world (every registered global and what it points to);
//   - a rollback check: save, run K ticks, hash; restore, run the same K ticks, hash. Equal hashes mean the
//     registered state is the whole simulation state and a resimulation is deterministic.
// Then it exits.
#include "Game.hpp"
#include "AmbulanceService.hpp"
#include "Car.hpp"
#include "Char_Pool.hpp"
#include "CollisionSoundQueue.hpp"
#include "Cranes.hpp"
#include "Crushers.hpp"
#include "DoorPool.hpp"
#include "EmergencyCrewPool.hpp"
#include "ExplodingScore_100.hpp"
#include "Firefighters.hpp"
#include "Garage.hpp"
#include "Generators.hpp"
#include "Globals.hpp"
#include "Hud.hpp"
#include "Light.hpp"
#include "LightPool.hpp"
#include "Object_5C.hpp"
#include "ParticleManager.hpp"
#include "Ped.hpp"
#include "Player.hpp"
#include "Police.hpp"
#include "PublicTransport.hpp"
#include "ScriptManager.hpp"
#include "TileAnimManager.hpp"
#include "TrafficLights.hpp"
#include "collide.hpp"
#include "debug.hpp"
#include "winmain.hpp"
#include "ObjectPool.hpp"
#include "../port_globals.hpp"
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

void StateLog_Frame(unsigned flip);

EXTERN_GLOBAL(Light**, gLightGrid_6F5400);

namespace
{
typedef std::chrono::steady_clock Clock;

struct Block
{
    unsigned char* addr;
    size_t size;
    const char* name;
};

// The registered globals and their pointees, as the state log sees them
std::vector<Block> WorldBlocks()
{
    std::vector<Block> blocks;
    size_t count = 0;
    const PortGlobal* const* globals = PortGlobals(&count);
    for (size_t i = 0; i < count; i++)
    {
        const PortGlobal& g = *globals[i];
        Block b = {(unsigned char*)g.mVar, g.mSize, g.mName};
        blocks.push_back(b);
        if (g.mPointeeSize)
        {
            unsigned char* p;
            memcpy(&p, g.mVar, sizeof(p));
            if (p)
            {
                Block pb = {p, g.mPointeeSize, g.mName};
                blocks.push_back(pb);
            }
        }
    }
    // Heap objects no global points to directly: the players (Game::field_4_players)
    for (int i = 0; i < MAX_PLAYERS && gGame_0x40_67E008; i++)
    {
        if (Player* p = gGame_0x40_67E008->field_4_players[i])
        {
            Block b = {(unsigned char*)p, sizeof(Player), "Player"};
            blocks.push_back(b);
        }
    }
    // Globals that point to an array: the registry knows one element (ponytail: found by the rollback check, add more as it finds them)
    if (gLightGrid_6F5400)
    {
        Block b = {(unsigned char*)gLightGrid_6F5400, 0x1000 * sizeof(Light*), "gLightGrid_6F5400[]"};
        blocks.push_back(b);
    }
    if (getenv("GTA2_SIMBENCH_UNSIZED")) // pointer globals whose pointee isn't in the world (incomplete type)
    {
        for (size_t i = 0; i < count; i++)
        {
            void* p;
            if (globals[i]->mSize == sizeof(p) && !globals[i]->mPointeeSize && (memcpy(&p, globals[i]->mVar, sizeof(p)), p))
            {
                fprintf(stderr, "simbench: unsized pointer %s\n", globals[i]->mName);
            }
        }
    }
    return blocks;
}

size_t Save(const std::vector<Block>& blocks, std::vector<unsigned char>& buf)
{
    size_t n = 0;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        n += blocks[i].size;
    }
    buf.resize(n);
    n = 0;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        memcpy(&buf[n], blocks[i].addr, blocks[i].size);
        n += blocks[i].size;
    }
    return n;
}

void Restore(const std::vector<Block>& blocks, const std::vector<unsigned char>& buf)
{
    size_t n = 0;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        memcpy(blocks[i].addr, &buf[n], blocks[i].size);
        n += blocks[i].size;
    }
}

const int kServices = 22;
const char* kNames[kServices] = {"players", "queues", "lights", "cranes", "crushers", "generators", "peds", "transport",
                                 "garages", "cars", "traffic_lights", "objects", "scripts", "emergency", "ambulance",
                                 "police", "particles", "tile_anims", "hud", "rng", "doors", "firefighters+scores"};
double gNs[kServices];

// Game::UpdateGame_4B9410, timed per service (bDo_mike, show_timing, junction ids: debug only, left out)
void TimedTick()
{
    Clock::time_point t = Clock::now();
    int s = 0;
#define LAP()                                                                                            \
    do                                                                                                   \
    {                                                                                                    \
        const Clock::time_point now = Clock::now();                                                      \
        gNs[s++] += std::chrono::duration<double, std::nano>(now - t).count();                          \
        t = now;                                                                                         \
    } while (0)
    Game* g = gGame_0x40_67E008;
    for (u32 i = 0; i < g->field_23_num_players; i++)
    {
        if (g->field_4_players[i]->field_8E_in_use)
        {
            g->field_4_players[i]->Service_5687F0();
        }
    }
    LAP();
    gCollisionSoundQueue_66AFE0->Reset_40BB90();
    gCollisionCounters_6791FC->ResetCount_478A20();
    LAP();
    if (gLighting_626A09)
    {
        gLight_1D4CC_6F5520->Service_45C1E0();
    }
    LAP();
    gCranePool_D9C_679FD4->CranesService_480E50();
    LAP();
    gCrusherPool_94_67A830->CrushersService_4887F0();
    LAP();
    gGeneratorPool_14AC_67E5D0->GeneratorsService_4C1D70();
    LAP();
    gPedManager_6787BC->PedsService_4703F0();
    LAP();
    gPublicTransport_181C_6FF1D4->PublicTransportService_57A7A0();
    LAP();
    gGarage_48_6FD26C->GaragesService_5349D0();
    LAP();
    gCarManager_6C_677930->CarsService_446790();
    LAP();
    if (!bSkip_traffic_lights_67D4EC)
    {
        gTrafficLights_194_705958->TrafficLightsService_5C2950();
    }
    LAP();
    gObject_5C_6F8F84->ObjectsService_5293A0();
    LAP();
    if (!bSkip_mission_67D4E5)
    {
        gScriptManager_6F8060->ExecuteScriptThreads_5127A0();
    }
    LAP();
    gEmergencyCrewPool_706280->ServiceAll_5CBBD0();
    LAP();
    if (!bSkip_ambulance_67D6C9)
    {
        gAmbulance_110_6F70A8->AmbulancesService_4FA790();
    }
    LAP();
    if (!bSkip_police_67D4F9)
    {
        gPolice_7B8_6FEE40->Service_570270();
    }
    LAP();
    if (!bSkip_particles_67D64D)
    {
        gParticle_8_6FD5E8->ParticlesService_53E320();
    }
    LAP();
    gTileAnim_2_7052C4->UpdateTileAnimations_5BC310();
    LAP();
    gHud_2B00_706620->UpdateHUD_5D69D0();
    LAP();
    gpRng_67AB34->AdvanceCycle_48B900();
    LAP();
    gDoor_4D4_67BD2C->DoorsService_49D460();
    LAP();
    gFirefighterPool_54_67D4C0->FireEnginesService_4A85F0();
    if (!bExplodingScoresOff_67D4FB)
    {
        gExplodingScorePool->Update_596940();
    }
    LAP();
#undef LAP
}

unsigned long long Hash(const std::vector<Block>& blocks)
{
    unsigned long long h = 1469598103934665603ull;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        for (size_t j = 0; j < blocks[i].size; j++)
        {
            h = (h ^ blocks[i].addr[j]) * 1099511628211ull;
        }
    }
    return h;
}

template <class T> int Used(T* pFirst)
{
    int n = 0;
    for (; pFirst; pFirst = pFirst->mpNext)
    {
        n++;
    }
    return n;
}

void Counts(const char* when)
{
    fprintf(stderr, "simbench: %s: %d cars, %d peds, %d objects\n", when, Used(gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev),
            Used(gPedPool_6787B8->field_0_pool.field_4_pPrev), Used(gObject_2C_Pool_6F8F80->field_4_pPrev));
}

double Ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

void Run(int ticks)
{
    std::vector<Block> blocks = WorldBlocks();
    std::vector<unsigned char> saved, scratch;

    // Save/restore cost
    const int reps = 50;
    Clock::time_point a = Clock::now();
    size_t bytes = 0;
    for (int i = 0; i < reps; i++)
    {
        bytes = Save(blocks, scratch);
    }
    Clock::time_point b = Clock::now();
    for (int i = 0; i < reps; i++)
    {
        Restore(blocks, scratch);
    }
    Clock::time_point c = Clock::now();
    fprintf(stderr, "simbench: world %zu blocks, %.2f MB; save %.3f ms, restore %.3f ms\n", blocks.size(),
            bytes / 1048576.0, Ms(a, b) / reps, Ms(b, c) / reps);

    // Rollback check: the same K ticks twice from one saved state
    const int k = 30;
    Save(blocks, saved);
    for (int i = 0; i < k; i++)
    {
        gGame_0x40_67E008->UpdateGame_4B9410();
    }
    std::vector<unsigned char> first;
    Save(blocks, first);
    const unsigned long long h1 = Hash(blocks);
    Restore(blocks, saved);
    for (int i = 0; i < k; i++)
    {
        gGame_0x40_67E008->UpdateGame_4B9410();
    }
    const unsigned long long h2 = Hash(blocks);
    fprintf(stderr, "simbench: rollback of %d ticks %s\n", k, h1 == h2 ? "reproduces the world" : "DIFFERS:");
    if (h1 != h2)
    {
        size_t n = 0;
        int shown = 0;
        for (size_t i = 0; i < blocks.size() && shown < 40; i++)
        {
            if (memcmp(&first[n], blocks[i].addr, blocks[i].size))
            {
                size_t off = 0;
                while (first[n + off] == blocks[i].addr[off])
                {
                    off++;
                }
                size_t differ = 0, last = off;
                for (size_t j = off; j < blocks[i].size; j++)
                {
                    if (first[n + j] != blocks[i].addr[j])
                    {
                        differ++;
                        last = j;
                    }
                }
                fprintf(stderr, "  %s%s +0x%zx..0x%zx (%zu bytes differ of 0x%zx)\n", i && blocks[i - 1].name == blocks[i].name ? "*" : "",
                        blocks[i].name, off, last, differ, blocks[i].size);
                shown++;
            }
            n += blocks[i].size;
        }
    }

    // Tick cost, per service. GTA2_SIMBENCH_NORECYCLE: cars aren't despawned (debug skip_recycling), with counts
    // and the tick cost every 500 ticks, to see the world fill up
    const bool norecycle = getenv("GTA2_SIMBENCH_NORECYCLE") != NULL;
    bSkip_recycling_67D575 |= norecycle;
    Counts("before");
    a = Clock::now();
    Clock::time_point w = a;
    for (int i = 0; i < ticks; i++)
    {
        TimedTick();
        if (norecycle && (i + 1) % 500 == 0)
        {
            const Clock::time_point now = Clock::now();
            char when[64];
            snprintf(when, sizeof(when), "tick %d, %.1f us per tick", i + 1, Ms(w, now) * 1000.0 / 500);
            Counts(when);
            w = now;
        }
    }
    b = Clock::now();
    const double perTick = Ms(a, b) / ticks;
    Counts("after");
    fprintf(stderr, "simbench: %d ticks, %.3f ms per tick (%.0f ticks/s)\n", ticks, perTick, 1000.0 / perTick);
    for (int s = 0; s < kServices; s++)
    {
        fprintf(stderr, "  %-20s %8.1f us %5.1f%%\n", kNames[s], gNs[s] / ticks / 1000.0, gNs[s] / ticks / 1e4 / perTick);
    }
}
} // namespace

void SimBench_Frame(unsigned flip)
{
    static const char* spec = getenv("GTA2_SIMBENCH");
    if (!spec || !gGame_0x40_67E008 || !gpRng_67AB34 || !gPedPool_6787B8)
    {
        return;
    }
    // p<n>: once n peds are alive (a busy scene, e.g. an attract demo), else at flip <n>
    if (spec[0] == 'p' ? Used(gPedPool_6787B8->field_0_pool.field_4_pPrev) < atoi(spec + 1) : (unsigned)atoi(spec) != flip)
    {
        return;
    }
    const char* colon = strchr(spec, ':');
    Run(colon ? atoi(colon + 1) : 3000);
    exit(0);
}
