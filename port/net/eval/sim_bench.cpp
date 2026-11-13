// Port (docs/netcode.md, "Measurements behind the design"): GTA2_SIMBENCH=<flip>[:<ticks>] measures the simulation for client-side prediction.
// At that flip (or GTA2_SIMBENCH=p<n>: once n peds are alive; in a game) it runs <ticks> (default 3000) sim ticks back to back, without drawing or audio, and
// prints to stderr:
//   - the time per tick of each subsystem service in Game::UpdateGame (the same calls in the same order);
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
#include "port/src/port_globals.hpp"
#include "../engine/world.hpp"
#include "neteval.hpp"
#include "sim_bench.hpp"
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

EXTERN_GLOBAL(Light**, gLightGrid);
EXTERN_GLOBAL(GridSpriteLink_Pool*, gGridSpriteLink_Pool);

namespace
{
typedef std::chrono::steady_clock Clock;

using net::engine::Block;
using net::engine::HashWorld;
using net::engine::RestoreWorld;
using net::engine::SaveWorld;

// The rollback set (port/net/engine/world.cpp), and the bench's diagnostics on it
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

// Game::UpdateGame, timed per service (bDo_mike, show_timing, junction ids: debug only, left out). A copy of its
// calls in their order: when the decomp's UpdateGame changes, this has to follow.
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
    Game* g = gGame_0x40;
    for (u32 i = 0; i < g->mNumPlayers; i++)
    {
        if (g->mPlayers[i]->mInUse)
        {
            g->mPlayers[i]->Service();
        }
    }
    LAP();
    gCollisionSoundQueue->Reset();
    gCollisionCounters->ResetCount();
    LAP();
    if (gLighting)
    {
        gLight_1D4CC->Service();
    }
    LAP();
    gCranePool_D9C->CranesService();
    LAP();
    gCrusherPool_94->CrushersService();
    LAP();
    gGeneratorPool_14AC->GeneratorsService();
    LAP();
    gPedManager->PedsService();
    LAP();
    gPublicTransport_181C->PublicTransportService();
    LAP();
    gGarage_48->GaragesService();
    LAP();
    gCarManager_6C->CarsService();
    LAP();
    if (!bSkip_traffic_lights)
    {
        gTrafficLights_194->TrafficLightsService();
    }
    LAP();
    gObject_5C->ObjectsService();
    LAP();
    if (!bSkip_mission)
    {
        gScriptManager->ExecuteScriptThreads();
    }
    LAP();
    gEmergencyCrewPool->ServiceAll();
    LAP();
    if (!bSkip_ambulance)
    {
        gAmbulance_110->AmbulancesService();
    }
    LAP();
    if (!bSkip_police)
    {
        gPolice_7B8->Service();
    }
    LAP();
    if (!bSkip_particles)
    {
        gParticle_8->ParticlesService();
    }
    LAP();
    gTileAnim_2->UpdateTileAnimations();
    LAP();
    gHud_2B00->UpdateHUD();
    LAP();
    gpRng->AdvanceCycle();
    LAP();
    gDoor_4D4->DoorsService();
    LAP();
    gFirefighterPool_54->FireEnginesService();
    if (!bExplodingScoresOff)
    {
        gExplodingScorePool->Update();
    }
    LAP();
#undef LAP
}

// The length of a pool's list: its used entries from mpPrev, its free ones from the head of the free list
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
            ListLength(gCar_BC_Pool->mPool.mpPrev), ListLength(gPedPool->mPool.mpPrev),
            ListLength(gObject_2C_Pool->mpPrev), ListLength(gGridSpriteLink_Pool->mpHead),
            ListLength(gSpritePool->mPool.mpHead));
}

// GTA2_SIMBENCH_FILL=<cars>: before measuring, fill the map with traffic by the game's own spawner. The first
// player's camera moves to random places, the traffic cap (16 ambient cars for the whole game,
// CarManager::CanAllocateCarOfKind) and the cooldown between spawns are lifted for the call, and recycling is off so nothing despawns.
// Stops at <cars> cars, or before the ped pool runs out (every traffic car has a driver).

// The camera's view, padded view and position, moved by (dx, dy) from where they were
void ShiftCamera(Camera* cam, const WorldRect& padded, const WorldRect& view, const CameraPos& pos, const Fix16& dx, const Fix16& dy)
{
    cam->mPaddedViewBounds.mLeft = padded.mLeft + dx;
    cam->mPaddedViewBounds.mRight = padded.mRight + dx;
    cam->mPaddedViewBounds.mTop = padded.mTop + dy;
    cam->mPaddedViewBounds.mBottom = padded.mBottom + dy;
    cam->mViewBounds.mLeft = view.mLeft + dx;
    cam->mViewBounds.mRight = view.mRight + dx;
    cam->mViewBounds.mTop = view.mTop + dy;
    cam->mViewBounds.mBottom = view.mBottom + dy;
    cam->mCurPos.mX = pos.mX + dx;
    cam->mCurPos.mY = pos.mY + dy;
}

void Fill(int targetCars)
{
    Camera* cam = gGame_0x40->IteratePlayerCamera();
    Player* player = gGame_0x40->IterateFirstPlayer();
    TrafficSpawner* sp = gTrafficSpawner_14;
    if (!cam || !player)
    {
        return;
    }
    const WorldRect padded = cam->mPaddedViewBounds, view = cam->mViewBounds;
    const CameraPos pos = cam->mCurPos;
    const Fix16 cx = (padded.mLeft + padded.mRight) / 2, cy = (padded.mTop + padded.mBottom) / 2;
    fprintf(stderr, "simbench: fill: traffic %d, proto %d, parked %d, spawn block %d, limit_recycling %d\n",
            gCarManager_6C->mRecycledCars, gCarManager_6C->mProtoRecycledCars,
            gCarManager_6C->mParkedCars, (int)gCarManager_6C->mSpawnBlock, (int)bLimit_recycling);
    unsigned seed = 12345;
    int attempts = 0;
    while (ListLength(gCar_BC_Pool->mPool.mpPrev) < targetCars && attempts++ < 200000 &&
           ListLength(gPedPool->mPool.mpStart) > 8 && ListLength(gCar_BC_Pool->mPool.mpStart) > 8)
    {
        seed = seed * 1103515245u + 12345u;
        const Fix16 x = Fix16((s32)(16 + (seed >> 8) % 224)), y = Fix16((s32)(16 + (seed >> 20) % 224));
        ShiftCamera(cam, padded, view, pos, x - cx, y - cy);
        sp->mCam = cam;
        sp->mPlayer = player;
        sp->mZone = gMap_0x370->GetZoneInfoAt(x.ToInt(), y.ToInt());
        const s32 traffic = gCarManager_6C->mRecycledCars, proto = gCarManager_6C->mProtoRecycledCars;
        gCarManager_6C->mRecycledCars = 0;
        gCarManager_6C->mProtoRecycledCars = 0;
        gCarManager_6C->mSpawnBlock = 0; // the cooldown between two spawns
        player->mTrafficSpawnCounter = 0xFFFF; // and the player's spawn pacing (SelectTrafficCarModel)
        sp->MakeTrafficForCurrCamera();
        gCarManager_6C->mRecycledCars += traffic;
        gCarManager_6C->mProtoRecycledCars += proto;
    }
    cam->mPaddedViewBounds = padded;
    cam->mViewBounds = view;
    cam->mCurPos = pos;
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
        gGame_0x40->UpdateGame();
    }
    SaveWorld(blocks, first);
    const unsigned long long h1 = HashWorld(blocks);
    RestoreWorld(blocks, saved);
    for (int i = 0; i < k; i++)
    {
        gGame_0x40->UpdateGame();
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
        if (norecycle) // no ped cleanup either: every ped counts as seen (Ped::StateMachineTick's off-screen timers)
        {
            for (Ped* p = gPedPool->mPool.mpPrev; p; p = p->mpNext)
            {
                p->mOffscreenCounter = 0;
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
    bSkip_recycling |= norecycle;
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
    if (!spec || !gGame_0x40 || !gpRng || !gPedPool)
    {
        return;
    }
    // p<n>: once n peds are alive (a busy scene, e.g. an attract demo), else at flip <n>
    if (spec[0] == 'p' ? ListLength(gPedPool->mPool.mpPrev) < atoi(spec + 1) : (unsigned)atoi(spec) != flip)
    {
        return;
    }
    const char* colon = strchr(spec, ':');
    Run(colon ? atoi(colon + 1) : 3000);
    exit(0);
}
