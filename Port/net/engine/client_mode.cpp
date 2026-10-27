// Port/net (docs/netcode.md, "What the client runs"): a client simulates what moves, not what decides. Spawning,
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
    // A chase task (police cars boxing in a wanted player) whose driver has no car with it now: its car went (the
    // server's) or is another (the snapshot's). CarAI::UpdateStateMachine_44E560 found that driver in the table
    // (FindDriverInState_4748A0) and read its car's task. Freed, and no car keeps it (as AmbulanceService does)
    std::set<Ped*> peds;
    std::set<Car*> cars;
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        peds.insert(p);
    }
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        cars.insert(c);
    }
    for (CarChaseTask& t : gCarChaseTaskTable_678E30->field_0_entries)
    {
        Ped* d = t.field_4_pDriver;
        Car* car = d && peds.count(d) ? d->field_16C_car : NULL;
        if (t.field_0_in_use == 1 && !(car && cars.count(car) && car->field_60_pChaseTask == &t))
        {
            for (Car* c : cars)
            {
                c->field_60_pChaseTask = c->field_60_pChaseTask == &t ? NULL : c->field_60_pChaseTask;
            }
            gCarChaseTaskTable_678E30->FreeEntry_474CC0(&t);
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        // A ped on foot in the car state (the snapshot's: the server's got in, its entry here another) with no car to get
        // into: Ped::UpdateCharB4_462B80 seats it in its char's target car, and read none. Its own car, or on foot
        Char* body = p->field_168_game_object;
        if (body && p->field_278_ped_state_1 == ped_state_1::in_car_10 && !(body->field_84_target_car && cars.count(body->field_84_target_car)))
        {
            if (p->field_16C_car && cars.count(p->field_16C_car))
            {
                body->field_84_target_car = p->field_16C_car;
            }
            else
            {
                p->field_278_ped_state_1 = ped_state_1::walking_0;
            }
        }
        if (p->field_16C_car && !p->field_168_game_object &&
            (OnFootOnly(p->field_258_objective) || OnFootOnly(p->field_25C_internal_objective)))
        {
            p->field_258_objective = objectives_enum::no_obj_0;
            p->field_25C_internal_objective = objectives_enum::no_obj_0;
        }
        // A fire engine's crew told to aim its water cannon from a car without one, or at no car (here its car was
        // another than the server's, or the order came first): Ped::AimVehicleTurretStateMachine_46A6D0 read the cannon
        const bool aiming = p->field_258_objective == objectives_enum::turret_put_out_car_fire_60 ||
                            p->field_25C_internal_objective == objectives_enum::turret_put_out_car_fire_60;
        if (aiming && (!p->field_150_target_objective_car || !p->field_16C_car ||
                       !p->field_16C_car->field_0_attachments.GetSpriteForModel_5A6A50(objects::water_cannon_114)))
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

// An arrow to each other player in the game. The game makes them when the HUD starts (Hud::Init_5D6BE0: those in the
// game then) and drops one for good once its player is out of the game for a tick (ArrowTrace::UpdateAimCoordinates_5D03F0):
// here players join after the start, and come and go with the snapshots, so a missing one is made again (all of them,
// as the game makes them), before each tick, the resimulated ones too
void net::engine::KeepPlayerArrows()
{
    if (!gHud_2B00_706620 || !gGame_0x40_67E008 || gGameSession_67E8E0.GetMultiplayerGamemode_4C5BC0() == TAG_GAME_3)
    {
        return; // the tag game: an arrow to the fugitive alone (TagGame.cpp)
    }
    Hud_Arrow_7C_Array& arrows = gHud_2B00_706620->field_1F18_arrows;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player* p = gGame_0x40_67E008->field_4_players[i];
        if (!p || p->field_0_is_user || !p->field_8E_in_use || !p->field_2C4_player_ped)
        {
            continue;
        }
        bool found = false;
        for (Hud_Arrow& a : arrows.field_0_array)
        {
            const ArrowTrace& t = a.field_18_targets.field_18_primary_target;
            found = found || (a.field_18_targets.field_10_gang_info.field_6_in_use && t.field_10_target_type == ArrowTargetType::Player_6 && t.field_C_player == p);
        }
        if (!found)
        {
            static unsigned made;
            if (++made <= 20 || made % 100 == 0)
            {
                printf("net: the arrows to the other players made again (%u times), player %d's was missing\n", made, i);
            }
            arrows.CreatePlayerArrows_5D1350();
            return;
        }
    }
}

void net::engine::AnchorAttachments()
{
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        Sprite* car = c->field_50_car_sprite;
        for (AttachedSprite* a = car ? c->field_0_attachments.field_0_p18 : NULL; a; a = a->mpNext)
        {
            Object* o = a->field_0_pSprite ? a->field_0_pSprite->AsObject2C_40FEC0() : NULL;
            if (!o || !o->field_4_sprite || o->field_25_removal_state)
            {
                continue;
            }
            // As the game does: the offset turned with the car, its rotation on top of the car's
            Fix16 x = car->field_14_xy.x, y = car->field_14_xy.y;
            if (a->field_8_offset_x != Fix16(0) || a->field_C_offset_y != Fix16(0))
            {
                x = a->field_8_offset_x;
                y = a->field_C_offset_y;
                Ang16 rot = car->field_0_rotation;
                Ang16::RotateVector_41FC90(x, y, rot);
                x += car->field_14_xy.x;
                y += car->field_14_xy.y;
            }
            o->field_4_sprite->set_xyz_lazy_420600(x, y, car->field_1C_zpos);
            o->field_4_sprite->set_ang_lazy_420690(car->field_0_rotation + a->field_10_offset_rot);
        }
    }
}
