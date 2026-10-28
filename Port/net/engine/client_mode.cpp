// Port/net (docs/netcode.md, "The client"): a client simulates what moves, not what decides. Spawning,
// despawning, scripts and the emergency services are the server's; the game's own debug switches turn them off,
// so the decomp needs no hooks for it. Network games clear those switches at the start (ClearDebugFlags_4DB170),
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
#include "Port/port_hooks.hpp"
#include <set>

void net::engine::ProtectPlayers()
{
    for (int i = 0; i < PlayerCount(); i++)
    {
        Ped* ped = InGamePed(i);
        if (ped && ped->field_208_invulnerability < 2)
        {
            ped->field_208_invulnerability = 2; // counts down once per tick: never 0 between our frames
        }
        // The car a player sits in or is getting into stays: one marked for removal before (it wasn't the server's,
        // or not yet) would otherwise go with the player in it, as Ped::SpawnPedInCar_45C730 knows
        Car* cars[2] = {ped ? ped->field_16C_car : NULL, ped ? ped->field_150_target_objective_car : NULL};
        for (int k = 0; k < 2; k++)
        {
            if (cars[k] && cars[k]->field_88_despawn_status != car_despawn_status::active_1)
            {
                cars[k]->field_88_despawn_status = car_despawn_status::active_1;
            }
        }
    }
}

void net::engine::ClientModeFrame()
{
    ProtectPlayers();
    GuardPeds();
    KeepPlayerArrows(); // in the tick: its saves have it, so a rollback doesn't take it back
    bSkip_recycling_67D575 = true; // no traffic generation, no car despawning (TrafficSpawner::GenerateTraffic_583670)
    bSkip_dummies_67D4EF = true; // no ambient peds (PedManager::Dummies_470330)
    bSkip_mission_67D4E5 = true; // scripts run on the server only (they set up the level before the first frame)
    bSkip_police_67D4F9 = true;
    gPortKeepWantedPoints = true; // the server's (players' entities, peds' AI state)
    bSkip_ambulance_67D6C9 = true;
    SeenByAll();
}

// Everything counts as seen: a client never removes peds or corpses off its screen (Ped::StateMachineTick_4626B0,
// Ped::PoolUpdate) or cars (Car::ManageDespawning_442310); the server does. Before each resimulated tick too: the
// saves keep the counters, so once a frame wasn't enough (they went on counting along the saves, and a dead player's
// ped off screen 200 ticks was freed as a corpse: a crash at its respawn)
void net::engine::SeenByAll()
{
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        p->field_20E_offscreen_counter = 0;
    }
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        c->field_76_last_seen_timer = 0;
    }
}

