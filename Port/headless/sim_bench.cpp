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
#include "Camera.hpp"
#include "map_0x370.hpp"
#include "debug.hpp"
#include "winmain.hpp"
#include "ObjectPool.hpp"
#include "root_sound.hpp"
#include "explosion_type.hpp"
#include "CarAI.hpp"
#include "CarPhysics.hpp"
#include "WeaponManager.hpp"
#include "../port_globals.hpp"
#include "../net/engine/world.hpp"
#include "../port_rand_sound.h"
#include "../port_rand_state.h"
#include <chrono>
#if !defined(_WIN32)
    #include <cxxabi.h>
    #include <dlfcn.h>
    #include <execinfo.h>
#endif
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

void StateLog_Frame(unsigned flip);

EXTERN_GLOBAL(Light**, gLightGrid_6F5400);
namespace crt
{
extern void (*gPortHeapHook)(size_t size); // Source/crt_stubs.cpp
}
EXTERN_GLOBAL(GridSpriteLink_Pool*, gGridSpriteLink_Pool_679200);
EXTERN_GLOBAL(u8, spawnSideLocked_6787D5);
EXTERN_GLOBAL(u8, spawnCountLimit_6787D6);

namespace
{
typedef std::chrono::steady_clock Clock;

typedef net::engine::Block Block;

// The rollback set (Port/net/engine/world.cpp), and the bench's diagnostics on it
std::vector<Block> WorldBlocks()
{
    if (getenv("GTA2_SIMBENCH_UNSIZED")) // pointer globals whose pointee isn't in the world (incomplete type)
    {
        size_t count = 0;
        const PortGlobal* const* globals = PortGlobals(&count);
        for (size_t i = 0; i < count; i++)
        {
            void* p;
            if (globals[i]->mSize == sizeof(p) && !globals[i]->mPointeeSize && (memcpy(&p, globals[i]->mVar, sizeof(p)), p))
            {
                fprintf(stderr, "simbench: unsized pointer %s\n", globals[i]->mName);
            }
        }
    }
    return net::engine::WorldBlocks();
}

size_t Save(const std::vector<Block>& blocks, std::vector<unsigned char>& buf)
{
    return net::engine::SaveWorld(blocks, buf);
}

void Restore(const std::vector<Block>& blocks, const std::vector<unsigned char>& buf)
{
    net::engine::RestoreWorld(blocks, buf);
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
    return net::engine::HashWorld(blocks);
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

template <class T> int Free(T* pFirst)
{
    return Used(pFirst);
}

void Counts(const char* when)
{
    fprintf(stderr, "simbench: %s: %d cars, %d peds, %d objects; free grid links %d, sprites %d\n", when,
            Used(gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev), Used(gPedPool_6787B8->field_0_pool.field_4_pPrev),
            Used(gObject_2C_Pool_6F8F80->field_4_pPrev), Free(gGridSpriteLink_Pool_679200->field_0_pHead),
            Free(gSpritePool_703818->field_0_pool.field_0_pHead));
}

// GTA2_SIMBENCH_FILL=<cars>: before measuring, fill the map with traffic by the game's own spawner. The first
// player's camera moves to random places, the traffic cap (16 ambient cars for the whole game,
// CarManager::CanAllocateCarOfKind_446930) and the cooldown between spawns are lifted for the call, and recycling is off so nothing despawns.
// GTA2_SIMBENCH_FILL_PEDS: walking peds as well. Stops at <cars> cars, or before the ped pool runs out (every traffic car has a driver).
bool gPeds;

void Fill(int targetCars)
{
    gPeds = getenv("GTA2_SIMBENCH_FILL_PEDS") != NULL;
    Camera* cam = gGame_0x40_67E008->IteratePlayerCamera_4B9BC0();
    Player* player = gGame_0x40_67E008->IterateFirstPlayer_4B9CD0();
    TrafficSpawner* sp = gTrafficSpawner_14_677934;
    if (!cam || !player)
    {
        return;
    }
    const WorldRect padded = cam->field_20_padded_view_bounds, view = cam->field_78_view_bounds;
    const CameraPos pos = cam->field_98_cur_pos;
    const Fix16 cx = (padded.field_0_left + padded.field_4_right) / 2, cy = (padded.field_8_top + padded.field_C_bottom) / 2;
    fprintf(stderr, "simbench: fill: traffic %d, proto %d, parked %d, spawn block %d, limit_recycling %d\n",
            gCarManager_6C_677930->field_28_recycled_cars, gCarManager_6C_677930->field_40_proto_recycled_cars,
            gCarManager_6C_677930->field_44_parked_cars, (int)gCarManager_6C_677930->field_54_spawn_block, (int)bLimit_recycling_67D4CA);
    unsigned seed = 12345;
    int attempts = 0;
    while (Used(gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev) < targetCars && attempts++ < 200000 &&
           Free(gPedPool_6787B8->field_0_pool.field_0_pStart) > 8 && Free(gCar_BC_Pool_67792C->field_0_pool.field_0_pStart) > 8)
    {
        seed = seed * 1103515245u + 12345u;
        const Fix16 x = Fix16((s32)(16 + (seed >> 8) % 224)), y = Fix16((s32)(16 + (seed >> 20) % 224));
        const Fix16 dx = x - cx, dy = y - cy;
        cam->field_20_padded_view_bounds.field_0_left = padded.field_0_left + dx;
        cam->field_20_padded_view_bounds.field_4_right = padded.field_4_right + dx;
        cam->field_20_padded_view_bounds.field_8_top = padded.field_8_top + dy;
        cam->field_20_padded_view_bounds.field_C_bottom = padded.field_C_bottom + dy;
        cam->field_78_view_bounds.field_0_left = view.field_0_left + dx;
        cam->field_78_view_bounds.field_4_right = view.field_4_right + dx;
        cam->field_78_view_bounds.field_8_top = view.field_8_top + dy;
        cam->field_78_view_bounds.field_C_bottom = view.field_C_bottom + dy;
        cam->field_98_cur_pos.field_0_x = pos.field_0_x + dx;
        cam->field_98_cur_pos.field_4_y = pos.field_4_y + dy;
        sp->field_0_cam = cam;
        sp->field_C_player = player;
        sp->field_10_zone = gMap_0x370_6F6268->GetZoneInfoAt_4DF890(x.ToInt(), y.ToInt());
        const s32 traffic = gCarManager_6C_677930->field_28_recycled_cars, proto = gCarManager_6C_677930->field_40_proto_recycled_cars;
        gCarManager_6C_677930->field_28_recycled_cars = 0;
        gCarManager_6C_677930->field_40_proto_recycled_cars = 0;
        gCarManager_6C_677930->field_54_spawn_block = 0; // the cooldown between two spawns
        player->field_680_traffic_spawn_counter = 0xFFFF; // and the player's spawn pacing (SelectTrafficCarModel_444AB0)
        sp->MakeTrafficForCurrCamera_5832C0();
        gCarManager_6C_677930->field_28_recycled_cars += traffic;
        gCarManager_6C_677930->field_40_proto_recycled_cars += proto;
        if (gPeds) // walking peds too (PedManager::Dummies_470330 without its cap of 50 and the on-screen density)
        {
            spawnSideLocked_6787D5 = 0;
            spawnCountLimit_6787D6 = 2;
            gPedManager_6787BC->field_6_num_peds_on_screen = 0;
            gPedManager_6787BC->SpawnDummies_46EB60(cam);
        }
    }
    cam->field_20_padded_view_bounds = padded;
    cam->field_78_view_bounds = view;
    cam->field_98_cur_pos = pos;
    fprintf(stderr, "simbench: fill: %d attempts\n", attempts);
}


// ---- GTA2_NETEVAL=heap,rand,soundrollback,churn,diverge,single,explode (docs/netcode.md, "Evaluations"): measurements for the netcode
// design, run after the fill (if any) and before the tick benchmark. GTA2_NETEVAL_OUT=<file>: per-offset churn.

void Tick()
{
    gGame_0x40_67E008->UpdateGame_4B9410();
}

struct PoolView
{
    const char* name;
    unsigned char* base;
    size_t stride;
    size_t count;
};

std::vector<PoolView> Pools()
{
    std::vector<PoolView> v;
#define POOLVIEW(name, arr) \
    { \
        PoolView pv = {name, (unsigned char*)&(arr)[0], sizeof((arr)[0]), sizeof(arr) / sizeof((arr)[0])}; \
        v.push_back(pv); \
    }
    POOLVIEW("Car", gCar_BC_Pool_67792C->field_0_pool.field_8_pool);
    POOLVIEW("CarPhysics", gCarPhysicsPool_6FE3E0->field_4_pool);
    POOLVIEW("CarAI", gCarAI_78_Pool_677CF8->field_0_pool.field_4_pool);
    POOLVIEW("Ped", gPedPool_6787B8->field_0_pool.field_8_pool);
    POOLVIEW("Char", gChar_B4_Pool_6FDB44->field_0_pool.field_4_pool);
    POOLVIEW("Object", gObject_2C_Pool_6F8F80->field_8_pool);
    POOLVIEW("Weapon", gWeapon_30_Pool_707014->field_0_pool.field_8_pool);
    POOLVIEW("Sprite", gSpritePool_703818->field_0_pool.field_4_pool);
#undef POOLVIEW
    if (Player* p = gGame_0x40_67E008->field_4_players[0])
    {
        PoolView pv = {"Player", (unsigned char*)p, sizeof(Player), 1};
        v.push_back(pv);
    }
    return v;
}

// How many bytes of each entity type change per tick, and which offsets ever change (an upper bound for the
// net schema: derived and scratch fields change too)
void Churn(int ticks)
{
    std::vector<PoolView> pools = Pools();
    std::vector<std::vector<unsigned char> > prev(pools.size());
    std::vector<std::vector<unsigned> > perOffset(pools.size());
    std::vector<std::vector<char> > slotChanged(pools.size());
    std::vector<double> bytes(pools.size()), entities(pools.size());
    for (size_t i = 0; i < pools.size(); i++)
    {
        prev[i].assign(pools[i].base, pools[i].base + pools[i].stride * pools[i].count);
        perOffset[i].assign(pools[i].stride, 0);
        slotChanged[i].assign(pools[i].count, 0);
    }
    for (int t = 0; t < ticks; t++)
    {
        Tick();
        for (size_t i = 0; i < pools.size(); i++)
        {
            const PoolView& pv = pools[i];
            for (size_t slot = 0; slot < pv.count; slot++)
            {
                unsigned char* now = pv.base + slot * pv.stride;
                unsigned char* was = &prev[i][slot * pv.stride];
                if (!memcmp(now, was, pv.stride))
                {
                    continue;
                }
                entities[i]++;
                slotChanged[i][slot] = 1;
                for (size_t o = 0; o < pv.stride; o++)
                {
                    if (now[o] != was[o])
                    {
                        perOffset[i][o]++;
                        bytes[i]++;
                    }
                }
                memcpy(was, now, pv.stride);
            }
        }
    }
    FILE* out = getenv("GTA2_NETEVAL_OUT") ? fopen(getenv("GTA2_NETEVAL_OUT"), "w") : NULL;
    for (size_t i = 0; i < pools.size(); i++)
    {
        int offsets = 0, slots = 0;
        for (size_t o = 0; o < pools[i].stride; o++)
        {
            offsets += perOffset[i][o] != 0;
            if (out && perOffset[i][o])
            {
                fprintf(out, "%s %zu %u\n", pools[i].name, o, perOffset[i][o]);
            }
        }
        for (size_t s = 0; s < pools[i].count; s++)
        {
            slots += slotChanged[i][s];
        }
        fprintf(stderr, "neteval churn: %-10s size %4zu: %4d slots changed; per tick %6.1f entities change, %7.0f bytes (%.1f per changed entity); %4d offsets ever change\n",
                pools[i].name, pools[i].stride, slots, entities[i] / ticks, bytes[i] / ticks, entities[i] ? bytes[i] / entities[i] : 0.0, offsets);
    }
    if (out)
    {
        fclose(out);
    }
}

// Entities within the first player's camera view, padded by half its size on every side (a snapshot's area)
void InterestCounts()
{
    Camera* cam = gGame_0x40_67E008->IteratePlayerCamera_4B9BC0();
    const WorldRect r = cam->field_20_padded_view_bounds;
    const Fix16 w = (r.field_4_right - r.field_0_left) / 2, h = (r.field_C_bottom - r.field_8_top) / 2;
    const Fix16 l = r.field_0_left - w, rt = r.field_4_right + w, t = r.field_8_top - h, b = r.field_C_bottom + h;
#define INSIDE(xy) ((xy).x >= l && (xy).x <= rt && (xy).y >= t && (xy).y <= b)
    int cars = 0, peds = 0, objects = 0;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        cars += c->field_50_car_sprite && INSIDE(c->field_50_car_sprite->field_14_xy);
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        peds += p->field_168_game_object && p->field_168_game_object->field_80_sprite_ptr && INSIDE(p->field_168_game_object->field_80_sprite_ptr->field_14_xy);
    }
    for (Object* o = gObject_2C_Pool_6F8F80->field_4_pPrev; o; o = o->mpNext)
    {
        objects += o->field_4_sprite && INSIDE(o->field_4_sprite->field_14_xy);
    }
#undef INSIDE
    fprintf(stderr, "neteval interest: view %.1f x %.1f blocks, area %.1f x %.1f: %d cars, %d peds on foot, %d objects\n",
            (w * 2).ToFloat(), (h * 2).ToFloat(), (w * 4).ToFloat(), (h * 4).ToFloat(), cars, peds, objects);
}

struct Pos
{
    u32 id;
    Fix16 x, y;
};

void Positions(std::vector<Pos>& cars, std::vector<Pos>& peds)
{
    cars.clear();
    peds.clear();
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        if (c->field_50_car_sprite)
        {
            Pos p = {(u32)c->field_6C_car_id, c->field_50_car_sprite->field_14_xy.x, c->field_50_car_sprite->field_14_xy.y};
            cars.push_back(p);
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        Pos q = {p->field_200_id, p->get_cam_x(), p->get_cam_y()};
        peds.push_back(q);
    }
}

void Compare(const char* what, int k, const std::vector<Pos>& a, const std::vector<Pos>& b, u32 playerId)
{
    int n = 0, over1px = 0, overQuarter = 0, overOne = 0;
    double sum = 0, player = -1;
    for (size_t i = 0; i < a.size(); i++)
    {
        for (size_t j = 0; j < b.size(); j++)
        {
            if (a[i].id != b[j].id)
            {
                continue;
            }
            const double dx = (a[i].x - b[j].x).ToFloat(), dy = (a[i].y - b[j].y).ToFloat();
            const double d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, dist = d > e ? d : e;
            n++;
            sum += dist;
            over1px += dist > 1.0 / 64;
            overQuarter += dist > 0.25;
            overOne += dist > 1.0;
            if (a[i].id == playerId)
            {
                player = dist;
            }
            break;
        }
    }
    fprintf(stderr, "neteval diverge: %-4s after %2d ticks: %3d compared, %5.1f%% off by > 1 px, %5.1f%% > 1/4 block, %5.1f%% > 1 block, mean %.3f blocks%s",
            what, k, n, n ? 100.0 * over1px / n : 0, n ? 100.0 * overQuarter / n : 0, n ? 100.0 * overOne / n : 0, n ? sum / n : 0, player >= 0 ? "" : "\n");
    if (player >= 0)
    {
        fprintf(stderr, "; player %.3f\n", player);
    }
}

unsigned* RandState()
{
    size_t count = 0;
    const PortGlobal* const* globals = PortGlobals(&count);
    for (size_t i = 0; i < count; i++)
    {
        if (globals[i]->mOgAddr == 0x626FE0)
        {
            return (unsigned*)globals[i]->mVar;
        }
    }
    return NULL;
}

// The same K ticks from one state, once as is and once with a different random stream: how far apart the cars and
// peds end up, i.e. how fast a client's own simulation of other entities drifts from the server's
void Diverge(const std::vector<Block>& blocks)
{
    std::vector<unsigned char> s0;
    Save(blocks, s0);
    unsigned* rs = RandState();
    Ped* playerPed = gGame_0x40_67E008->field_4_players[0]->field_2C4_player_ped;
    const u32 playerId = playerPed ? playerPed->field_200_id : 0;
    const int ks[] = {1, 3, 10, 30, 90};
    for (int ki = 0; ki < 5; ki++)
    {
        std::vector<Pos> c1, p1, c2, p2;
        for (int pass = 0; pass < 2; pass++)
        {
            Restore(blocks, s0);
            if (pass == 1 && rs)
            {
                *rs ^= 0x5A5A5A5A;
            }
            for (int t = 0; t < ks[ki]; t++)
            {
                Tick();
            }
            Positions(pass ? c2 : c1, pass ? p2 : p1);
        }
        Compare("cars", ks[ki], c1, c2, 0);
        Compare("peds", ks[ki], p1, p2, playerId);
    }
    Restore(blocks, s0);
}


// Which game functions use the heap during ticks: anything allocated or freed there lives outside the rollback set
std::map<std::string, std::pair<unsigned, unsigned> > gHeapUse; // caller -> (allocations, frees)
bool gInHook;

void HeapHook(size_t size)
{
    if (gInHook)
    {
        return;
    }
    gInHook = true;
    std::string who = "?"; // Windows: no caller names (ponytail: CaptureStackBackTrace + DbgHelp if needed there)
#if !defined(_WIN32)
    void* frames[8];
    const int n = backtrace(frames, 8);
    for (int i = 2; i < n; i++) // skip the hook and crt::malloc/free
    {
        Dl_info info;
        if (dladdr(frames[i], &info) && info.dli_sname)
        {
            int status = 0;
            char* d = abi::__cxa_demangle(info.dli_sname, 0, 0, &status);
            std::string name = status == 0 && d ? d : info.dli_sname;
            free(d);
            if (name.find("operator new") == 0 || name.find("operator delete") == 0)
            {
                continue;
            }
            who = name.substr(0, name.find('('));
            break;
        }
    }
#endif
    std::pair<unsigned, unsigned>& c = gHeapUse[who];
    (size ? c.first : c.second)++;
    gInHook = false;
}

void HeapUse(int ticks)
{
    gHeapUse.clear();
    crt::gPortHeapHook = HeapHook;
    for (int t = 0; t < ticks; t++)
    {
        Tick();
        gRoot_sound_66B038.Service_40EFA0();
    }
    crt::gPortHeapHook = 0;
    fprintf(stderr, "neteval heap: %zu callers used the heap in %d ticks (with the sound service)%s\n", gHeapUse.size(), ticks,
            gHeapUse.empty() ? "" : ":");
    for (std::map<std::string, std::pair<unsigned, unsigned> >::iterator it = gHeapUse.begin(); it != gHeapUse.end(); ++it)
    {
        fprintf(stderr, "  %-60s %6u allocations %6u frees\n", it->first.c_str(), it->second.first, it->second.second);
    }
}

// Who draws random numbers: the sim tick against the sound service, and whether a resimulation with sound draws the same
void RandUse(int ticks)
{
    unsigned sim = 0, sound = 0;
    for (int t = 0; t < ticks; t++)
    {
        unsigned c = gPortRandCalls;
        Tick();
        sim += gPortRandCalls - c;
        c = gPortRandCalls;
        gRoot_sound_66B038.Service_40EFA0();
        sound += gPortRandCalls - c;
    }
    fprintf(stderr, "neteval rand: per tick %.1f draws in the sim, %.2f in the sound service\n", (double)sim / ticks, (double)sound / ticks);
}

// Milestone S (docs/netcode.md): the same 30 ticks twice from one state, both with the sound service, with sound's
// random numbers on their own stream: the world outside the sound layer must come out the same, and the heap intact
void SoundRollback(const std::vector<Block>& blocks)
{
    PortRandSound_Separate(true);
    std::vector<unsigned char> s0, w1, w2;
    Save(blocks, s0);
    for (int pass = 0; pass < 2; pass++)
    {
        if (pass)
        {
            Restore(blocks, s0);
        }
        for (int t = 0; t < 30; t++)
        {
            Tick();
            gRoot_sound_66B038.Service_40EFA0();
        }
        Save(blocks, pass ? w2 : w1);
    }
    size_t n = 0, differ = 0;
    std::string names;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        const std::string name = blocks[i].name;
        const bool sound = name.find("ound") != std::string::npos; // gSound_obj, gRoot_sound, the SoundPosEntry pool
        if (!sound && memcmp(&w1[n], &w2[n], blocks[i].size))
        {
            differ++;
            names += " " + name;
        }
        n += blocks[i].size;
    }
    fprintf(stderr, "neteval soundrollback: 2 x 30 ticks with sound, own sound stream: %zu blocks outside sound differ%s\n", differ, names.c_str());
    PortRandSound_Separate(false);
}

// Can one object be advanced on its own (projectile rewind)? Rockets are fired around the player; then, per moving
// object, one full tick against that object's PoolUpdate alone, from the same state
void Single(const std::vector<Block>& blocks)
{
    Ped* ped = gGame_0x40_67E008->field_4_players[0]->field_2C4_player_ped;
    if (!ped)
    {
        return;
    }
    for (int i = 0; i < 16; i++)
    {
        const Ang16 a((s16)(i * 90), (u8)0);
        gObject_5C_6F8F84->NewMovingObj_52A240(objects::rocket_bullet_128, ped->get_cam_x() + Fix16(1), ped->get_cam_y(), ped->field_1AC_cam.z + Fix16(0.5f),
                                               a, a, Fix16(0.1f), Fix16(0), Fix16(0));
    }
    Tick();
    Tick();
    std::vector<Object*> moving;
    for (Object* o = gObject_2C_Pool_6F8F80->field_4_pPrev; o; o = o->mpNext)
    {
        if (o->field_4_sprite && o->field_8_definition)
        {
            moving.push_back(o);
        }
    }
    std::vector<unsigned char> s0;
    Save(blocks, s0);
    int same = 0, samePos = 0, moved = 0, n = 0;
    for (size_t i = 0; i < moving.size(); i++)
    {
        Object* o = moving[i];
        const Fix16 x0 = o->field_4_sprite->field_14_xy.x, y0 = o->field_4_sprite->field_14_xy.y;
        Tick();
        bool alive = false;
        for (Object* p = gObject_2C_Pool_6F8F80->field_4_pPrev; p && !alive; p = p->mpNext)
        {
            alive = p == o;
        }
        if (!alive || !o->field_4_sprite)
        {
            Restore(blocks, s0);
            continue; // removed during the tick
        }
        std::vector<unsigned char> full((unsigned char*)o, (unsigned char*)o + sizeof(Object));
        const Fix16 x1 = o->field_4_sprite->field_14_xy.x, y1 = o->field_4_sprite->field_14_xy.y;
        Restore(blocks, s0);
        if (x1 == x0 && y1 == y0)
        {
            continue; // not moving
        }
        moved++;
        const bool removed = o->PoolUpdate();
        n++;
        same += !removed && !memcmp(full.data(), o, sizeof(Object));
        samePos += !removed && o->field_4_sprite->field_14_xy.x == x1 && o->field_4_sprite->field_14_xy.y == y1;
        Restore(blocks, s0);
    }
    fprintf(stderr, "neteval single: %zu objects, %d moving; PoolUpdate alone gives the full tick's object bytes for %d, its position for %d\n",
            moving.size(), moved, same, samePos);
}

// Explosions at random cars, then the rollback check again
void Explode(int count)
{
    unsigned seed = 777;
    std::vector<Car*> cars;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        cars.push_back(c);
    }
    for (int i = 0; i < count && !cars.empty(); i++)
    {
        seed = seed * 1103515245u + 12345u;
        Car* c = cars[(seed >> 8) % cars.size()];
        gObject_5C_6F8F84->CreateExplosion_52A3D0(c->field_50_car_sprite->field_14_xy.x, c->field_50_car_sprite->field_14_xy.y,
                                                  c->field_50_car_sprite->field_1C_zpos, Ang16((s16)0, (u8)0), explosion_type::small_18, 0);
    }
    for (int t = 0; t < 10; t++)
    {
        Tick();
    }
    fprintf(stderr, "neteval explode: %d explosions\n", count);
}

void PlayerList()
{
    Game* g = gGame_0x40_67E008;
    fprintf(stderr, "neteval players: %d (local %d):", (int)g->field_23_num_players, (int)g->field_24_local_player_idx);
    for (u32 i = 0; i < g->field_23_num_players; i++)
    {
        Player* p = g->field_4_players[i];
        Ped* ped = p ? p->field_2C4_player_ped : NULL;
        if (ped)
        {
            fprintf(stderr, " [%u %s at %.1f,%.1f]", i, p->field_8E_in_use ? "in use" : "unused", ped->get_cam_x().ToFloat(), ped->get_cam_y().ToFloat());
        }
        else
        {
            fprintf(stderr, " [%u %s, no ped]", i, p && p->field_8E_in_use ? "in use" : "unused");
        }
    }
    fprintf(stderr, "\n");
}

void NetEval(const std::vector<Block>& blocks, const char* what)
{
    PlayerList();
    const std::string w = std::string(",") + what + ",";
#define HAS(name) (w.find("," name ",") != std::string::npos)
    if (HAS("explode"))
    {
        Explode(20);
    }
    InterestCounts();
    if (HAS("heap"))
    {
        HeapUse(300);
    }
    if (HAS("rand"))
    {
        RandUse(300);
    }
    if (HAS("soundrollback"))
    {
        SoundRollback(blocks);
    }
    if (HAS("diverge"))
    {
        Diverge(blocks);
    }
    if (HAS("single"))
    {
        Single(blocks);
    }
    if (HAS("churn"))
    {
        Churn(300);
    }
#undef HAS
}

double Ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

void RollbackCheck(const std::vector<Block>& blocks, const char* label)
{
    std::vector<unsigned char> saved;
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
    fprintf(stderr, "simbench: rollback of %d ticks (%s) %s\n", k, label, h1 == h2 ? "reproduces the world" : "DIFFERS:");
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
    Restore(blocks, saved);
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

    RollbackCheck(blocks, "at the start");

    // Tick cost, per service. GTA2_SIMBENCH_NORECYCLE: cars aren't despawned (debug skip_recycling), with counts
    // and the tick cost every 500 ticks, to see the world fill up
    const char* fill = getenv("GTA2_SIMBENCH_FILL");
    const bool norecycle = getenv("GTA2_SIMBENCH_NORECYCLE") != NULL || fill;
    bSkip_recycling_67D575 |= norecycle;
    if (fill)
    {
        Counts("before fill");
        Fill(atoi(fill));
        memset(gNs, 0, sizeof(gNs));
    }
    if (const char* ne = getenv("GTA2_NETEVAL"))
    {
        blocks = WorldBlocks(); // the fill and explosions may add pointees
        NetEval(blocks, ne);
        RollbackCheck(blocks, "after the evaluations");
    }
    Counts("before");
    a = Clock::now();
    Clock::time_point w = a;
    for (int i = 0; i < ticks; i++)
    {
        if (norecycle) // no ped cleanup either: every ped counts as seen (Ped::StateMachineTick_4626B0's off-screen timers)
        {
            for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
            {
                p->field_20E_offscreen_counter = 0;
            }
        }
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
