// Port/net (docs/netcode.md, "What the client runs"): a client simulates what moves, not what decides. Spawning,
// despawning, scripts and the emergency services are the server's; the game's own debug switches turn them off,
// so the decomp needs no hooks for it. Network games clear those switches at the start (ClearDebugFlags_4DB170),
// so they are set again every frame.
#include "engine.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Ped.hpp"
#include "Source/debug.hpp"

void net::engine::ClientModeFrame()
{
    bSkip_recycling_67D575 = true; // no traffic generation, no car despawning (TrafficSpawner::GenerateTraffic_583670)
    bSkip_dummies_67D4EF = true; // no ambient peds (PedManager::Dummies_470330)
    bSkip_mission_67D4E5 = true; // scripts run on the server only (they set up the level before the first frame)
    bSkip_police_67D4F9 = true;
    bSkip_ambulance_67D6C9 = true;
    // Everything counts as seen: a client never removes peds or corpses off its screen (Ped::StateMachineTick_4626B0,
    // Ped::PoolUpdate) or cars (Car::ManageDespawning_442310); the server does
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        p->field_20E_offscreen_counter = 0;
    }
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        c->field_76_last_seen_timer = 0;
    }
}
