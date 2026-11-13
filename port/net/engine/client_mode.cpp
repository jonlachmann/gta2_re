// port/net (docs/netcode.md, "The client"): a client simulates what moves, not what decides. Spawning,
// despawning, scripts and the emergency services are the server's; the game's own debug switches turn them off,
// so the decomp needs no hooks for it. Network games clear those switches at the start (ClearDebugFlags),
// so they are set again every frame.
#include "internal.hpp"
#include "Source/Game.hpp"
#include "Source/Player.hpp"
#include "Source/Car.hpp"
#include "Source/CarAI.hpp"
#include "Source/CarChaseTaskTable.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Ped.hpp"
#include "Source/Object_3C.hpp"
#include "Source/Object_5C.hpp"
#include "Source/sprite.hpp"
#include "Source/debug.hpp"
#include "Source/Police.hpp"
#include "Source/Hud.hpp"
#include "Source/GameSession.hpp"
#include "port/src/net_hooks.hpp"
#include <set>

void net::engine::ProtectPlayers()
{
    for (int i = 0; i < PlayerCount(); i++)
    {
        Ped* ped = InGamePed(i);
        if (ped && ped->mInvulnerability < 2)
        {
            ped->mInvulnerability = 2; // counts down once per tick: never 0 between our frames
        }
        // The car a player sits in or is getting into stays: one marked for removal before (it wasn't the server's,
        // or not yet) would otherwise go with the player in it, as Ped::SpawnPedInCar knows
        Car* cars[2] = {ped ? ped->mCar : NULL, ped ? ped->mTargetObjectiveCar : NULL};
        for (int k = 0; k < 2; k++)
        {
            if (cars[k] && cars[k]->mDespawnStatus != car_despawn_status::active_1)
            {
                cars[k]->mDespawnStatus = car_despawn_status::active_1;
            }
        }
    }
}

void net::engine::ClientModeFrame()
{
    ProtectPlayers();
    GuardPeds();
    KeepPlayerArrows(); // in the tick: its saves have it, so a rollback doesn't take it back
    bSkip_recycling = true; // no traffic generation, no car despawning (TrafficSpawner::GenerateTraffic)
    bSkip_dummies = true; // no ambient peds (PedManager::Dummies)
    bSkip_mission = true; // scripts run on the server only (they set up the level before the first frame)
    bSkip_police = true;
    // With the police off, every ped tick zeroes the wanted points (Ped::PoolUpdate): no cop heads, and the cops chasing a
    // player gave up between snapshots
    gPortKeepWantedPoints = true; // the server's (players' entities, peds' AI state)
    gPortServerKillNotices = true;
    bSkip_ambulance = true;
    SeenByAll();
}

// Everything counts as seen: a client never removes peds or corpses off its screen (Ped::StateMachineTick,
// Ped::PoolUpdate) or cars (Car::ManageDespawning); the server does. Before each resimulated tick too: the
// saves keep the counters, so once a frame wasn't enough (they went on counting along the saves, and a dead player's
// ped off screen 200 ticks was freed as a corpse: a crash at its respawn)
void net::engine::SeenByAll()
{
    for (Ped* p = gPedPool->mPool.mpPrev; p; p = p->mpNext)
    {
        p->mOffscreenCounter = 0;
    }
    for (Car* c = gCar_BC_Pool->mPool.mpPrev; c; c = c->mpNext)
    {
        c->mLastSeenTimer = 0;
    }
}

