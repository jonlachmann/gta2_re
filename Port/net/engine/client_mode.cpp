// Port/net (docs/netcode.md, "What the client runs"): a client simulates what moves, not what decides. Spawning,
// despawning, scripts and the emergency services are the server's; the game's own debug switches turn them off,
// so the decomp needs no hooks for it. Network games clear those switches at the start (ClearDebugFlags_4DB170),
// so they are set again every frame.
#include "internal.hpp"
#include "Source/Game.hpp"
#include "Source/Player.hpp"
#include "Source/Car.hpp"
#include "Source/CarAI.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Ped.hpp"
#include "Source/char.hpp"
#include "Source/debug.hpp"
#include "Source/Police.hpp"
#include "Port/port_hooks.hpp"
#include <unordered_set>

// Pulling someone out of a car is done on foot (Ped::PullDriverOutOfCarStateMachine_46B2F0 moves its char). A cop
// given that order through its group here (PedGroup::PromoteMemberToLeader_4C9680) and sat in a car since (the
// server's ped was there: the snapshot, or its own car-jack predicted differently) kept it, and crashed on it.
// So did a punch (Ped::MeleeAttackStateMachine_46B670) and an attack on foot (Ped::AttackTargetStateMachine_46D460)
namespace
{
bool OnFootOnly(s32 objective)
{
    return objective == objectives_enum::objective_32 || objective == objectives_enum::punch_char_23 ||
           objective == objectives_enum::kill_char_on_foot_20;
}

bool IsPassenger(Car* car, Ped* ped)
{
    for (PedListNode* n = car->field_4_passengers_list.field_0_pFirstPed; n; n = n->mpNext)
    {
        if (n->field_0_char_ped == ped)
        {
            return true;
        }
    }
    return false;
}
} // namespace

void net::engine::GuardPeds()
{
    // A network game's police stop at 1 star (Police's constructor): --police N lets them go to N, here and on
    // the server (a client's own crimes, predicted, add points up to it: Ped::add_wanted_points_470160)
    if (MaxWantedStars() > 1)
    {
        gPolice_7B8_6FEE40->field_660_max_wanted_stars = MaxWantedStars();
    }
    // Two players getting into one empty car by the same door at once: the second one's entry ends with it as the
    // driver, without pulling the first out, which sits on in it as no driver or passenger. The
    // car doesn't know it: blown up, it doesn't kill it, and it drives the wreck without physics (a crash). Out at
    // the side, as a car-jack would have put it
    for (int i = 0; i < PlayerCount(); i++)
    {
        Ped* ped = InGamePed(i);
        Car* car = ped ? ped->field_16C_car : NULL;
        if (car && ped->field_278_ped_state_1 == ped_state_1::in_car_10 && car->field_54_driver != ped && !IsPassenger(car, ped))
        {
            ForceOutBesideCar(ped, car);
        }
    }
    // A car's AI remembered a car it hit that has no sprite now (one going), and read its position on the next tick
    // (CarAI::ManageCollisions_452A20: a crash in a resimulation)
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        CarAI* ai = c->field_5C_AI;
        if (ai && ai->field_68_car_in_collision && !ai->field_68_car_in_collision->field_50_car_sprite)
        {
            ai->field_68_car_in_collision = NULL;
            ai->field_24_bf.bFlag12 = 0; // the collision to handle (it says there is a car)
        }
    }
    // A ped on foot after or at the door of a car that is going (no sprite now, or out of the pool: the server's
    // removal, more of them with more traffic): it read the car (Char::state_3_551A00, Ped::EnterTargetObjectiveCar_4686C0:
    // crashes). It stops and walks
    std::unordered_set<Car*> usable;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        if (c->field_50_car_sprite)
        {
            usable.insert(c);
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        Char* c = p->field_168_game_object;
        if (!c || p->field_278_ped_state_1 == ped_state_1::in_car_10)
        {
            continue;
        }
        Car** refs[4] = {&c->field_84_target_car, &p->field_150_target_objective_car, &p->field_154_target_to_enter, &p->field_16C_car};
        bool gone = false;
        for (Car** r : refs)
        {
            if (*r && !usable.count(*r))
            {
                *r = NULL;
                gone = true;
            }
        }
        if (gone)
        {
            p->SetObjective(objectives_enum::no_obj_0, 9999);
            p->SetObjective2_463830(objectives_enum::no_obj_0);
            p->ChangeNextPedState1_45C500(ped_state_1::walking_0);
            p->ChangeNextPedState2_45C540(ped_state_2::ped2_walking_0);
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        if (p->field_16C_car && !p->field_168_game_object &&
            (OnFootOnly(p->field_258_objective) || OnFootOnly(p->field_25C_internal_objective)))
        {
            p->field_258_objective = objectives_enum::no_obj_0;
            p->field_25C_internal_objective = objectives_enum::no_obj_0;
        }
    }
}

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
