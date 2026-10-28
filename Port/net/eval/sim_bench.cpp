// Port (docs/netcode.md, N0): GTA2_SIMBENCH=<flip>[:<ticks>] measures the simulation for client-side prediction.
// At that flip (or GTA2_SIMBENCH=p<n>: once n peds are alive; in a game) it runs <ticks> (default 3000) sim ticks back to back, without drawing or audio, and
// prints to stderr:
//   - the time per tick of each subsystem service in Game::UpdateGame_4B9410 (the same calls in the same order);
//   - the size and save/restore time of the full world (every registered global and what it points to);
//   - a rollback check: save, run K ticks, hash; restore, run the same K ticks, hash. Equal hashes mean the
//     registered state is the whole simulation state and a resimulation is deterministic;
//   - with GTA2_NETEVAL, the netcode's measurements (neteval.cpp) before the ticks.
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
#include "Port/port_globals.hpp"
#include "../engine/world.hpp"
#include "neteval.hpp"
#include "sim_bench.hpp"
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

EXTERN_GLOBAL(Light**, gLightGrid_6F5400);
EXTERN_GLOBAL(GridSpriteLink_Pool*, gGridSpriteLink_Pool_679200);
EXTERN_GLOBAL(u8, spawnSideLocked_6787D5);
EXTERN_GLOBAL(u8, spawnCountLimit_6787D6);

namespace
{
typedef std::chrono::steady_clock Clock;

using net::engine::Block;
using net::engine::HashWorld;
using net::engine::RestoreWorld;
using net::engine::SaveWorld;

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

const int kServices = 22;
const char* kNames[kServices] = {"players", "queues", "lights", "cranes", "crushers", "generators", "peds", "transport",
                                 "garages", "cars", "traffic_lights", "objects", "scripts", "emergency", "ambulance",
                                 "police", "particles", "tile_anims", "hud", "rng", "doors", "firefighters+scores"};
double gNs[kServices];

// Game::UpdateGame_4B9410, timed per service (bDo_mike, show_timing, junction ids: debug only, left out). A copy of its
// calls in their order: when the decomp's UpdateGame_4B9410 changes, this has to follow.
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

// The length of a pool's list: its used entries from field_4_pPrev, its free ones from the head of the free list
template <class T> int ListLength(T* pFirst)
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
    fprintf(stderr, "simbench: %s: %d cars, %d peds, %d objects; free grid links %d, sprites %d\n", when,
            ListLength(gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev), ListLength(gPedPool_6787B8->field_0_pool.field_4_pPrev),
            ListLength(gObject_2C_Pool_6F8F80->field_4_pPrev), ListLength(gGridSpriteLink_Pool_679200->field_0_pHead),
            ListLength(gSpritePool_703818->field_0_pool.field_0_pHead));
}

// GTA2_SIMBENCH_FILL=<cars>: before measuring, fill the map with traffic by the game's own spawner. The first
// player's camera moves to random places, the traffic cap (16 ambient cars for the whole game,
// CarManager::CanAllocateCarOfKind_446930) and the cooldown between spawns are lifted for the call, and recycling is off so nothing despawns.
// GTA2_SIMBENCH_FILL_PEDS: walking peds as well. Stops at <cars> cars, or before the ped pool runs out (every traffic car has a driver).
bool gPeds;

// The camera's view, padded view and position, moved by (dx, dy) from where they were
void ShiftCamera(Camera* cam, const WorldRect& padded, const WorldRect& view, const CameraPos& pos, const Fix16& dx, const Fix16& dy)
{
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
}

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
    while (ListLength(gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev) < targetCars && attempts++ < 200000 &&
           ListLength(gPedPool_6787B8->field_0_pool.field_0_pStart) > 8 && ListLength(gCar_BC_Pool_67792C->field_0_pool.field_0_pStart) > 8)
    {
        seed = seed * 1103515245u + 12345u;
        const Fix16 x = Fix16((s32)(16 + (seed >> 8) % 224)), y = Fix16((s32)(16 + (seed >> 20) % 224));
        ShiftCamera(cam, padded, view, pos, x - cx, y - cy);
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

double Ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// The first 40 blocks that differ between saved (SaveWorld's layout) and the world now
void ReportDifferences(const std::vector<Block>& blocks, const std::vector<unsigned char>& saved)
{
    size_t n = 0;
    int shown = 0;
    for (size_t i = 0; i < blocks.size() && shown < 40; i++)
    {
        if (memcmp(&saved[n], blocks[i].addr, blocks[i].size))
        {
            size_t off = 0;
            while (saved[n + off] == blocks[i].addr[off])
            {
                off++;
            }
            size_t differ = 0, last = off;
            for (size_t j = off; j < blocks[i].size; j++)
            {
                if (saved[n + j] != blocks[i].addr[j])
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

// The same K ticks twice from one saved state, then back to that state
void RollbackCheck(const std::vector<Block>& blocks, const char* label)
{
    const int k = 30;
    std::vector<unsigned char> saved, first;
    SaveWorld(blocks, saved);
    for (int i = 0; i < k; i++)
    {
        gGame_0x40_67E008->UpdateGame_4B9410();
    }
    SaveWorld(blocks, first);
    const unsigned long long h1 = HashWorld(blocks);
    RestoreWorld(blocks, saved);
    for (int i = 0; i < k; i++)
    {
        gGame_0x40_67E008->UpdateGame_4B9410();
    }
    const unsigned long long h2 = HashWorld(blocks);
    fprintf(stderr, "simbench: rollback of %d ticks (%s) %s\n", k, label, h1 == h2 ? "reproduces the world" : "DIFFERS:");
    if (h1 != h2)
    {
        ReportDifferences(blocks, first);
    }
    RestoreWorld(blocks, saved);
}

void SaveRestoreCost(const std::vector<Block>& blocks)
{
    const int reps = 50;
    std::vector<unsigned char> scratch;
    Clock::time_point a = Clock::now();
    size_t bytes = 0;
    for (int i = 0; i < reps; i++)
    {
        bytes = SaveWorld(blocks, scratch);
    }
    Clock::time_point b = Clock::now();
    for (int i = 0; i < reps; i++)
    {
        RestoreWorld(blocks, scratch);
    }
    Clock::time_point c = Clock::now();
    fprintf(stderr, "simbench: world %zu blocks, %.2f MB; save %.3f ms, restore %.3f ms\n", blocks.size(),
            bytes / 1048576.0, Ms(a, b) / reps, Ms(b, c) / reps);
}

// Tick cost, per service. norecycle: with counts and the tick cost every 500 ticks, to see the world fill up
void TickCost(int ticks, bool norecycle)
{
    Counts("before");
    const Clock::time_point a = Clock::now();
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
    const double perTick = Ms(a, Clock::now()) / ticks;
    Counts("after");
    fprintf(stderr, "simbench: %d ticks, %.3f ms per tick (%.0f ticks/s)\n", ticks, perTick, 1000.0 / perTick);
    for (int s = 0; s < kServices; s++)
    {
        fprintf(stderr, "  %-20s %8.1f us %5.1f%%\n", kNames[s], gNs[s] / ticks / 1000.0, gNs[s] / ticks / 1e4 / perTick);
    }
}

void Run(int ticks)
{
    std::vector<Block> blocks = WorldBlocks();
    SaveRestoreCost(blocks);
    RollbackCheck(blocks, "at the start");
    // GTA2_SIMBENCH_NORECYCLE: cars aren't despawned (debug skip_recycling)
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
    TickCost(ticks, norecycle);
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
    if (spec[0] == 'p' ? ListLength(gPedPool_6787B8->field_0_pool.field_4_pPrev) < atoi(spec + 1) : (unsigned)atoi(spec) != flip)
    {
        return;
    }
    const char* colon = strchr(spec, ':');
    Run(colon ? atoi(colon + 1) : 3000);
    exit(0);
}
