// port/net (port/docs/netcode.md, "The client"): the game's own logic made safe for a network game, on the server and the
// clients alike: peds given orders a network game breaks (a car-jack from a car, a punch from a seat), the arrows to
// the other players, what is attached to a car.
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

// Pulling someone out of a car is done on foot (Ped::PullDriverOutOfCarStateMachine moves its char). A cop
// given that order through its group here (PedGroup::PromoteMemberToLeader) and sat in a car since (the
// server's ped was there: the snapshot, or its own car-jack predicted differently) kept it, and crashed on it.
// So did a punch (Ped::MeleeAttackStateMachine) and an attack on foot (Ped::AttackTargetStateMachine)
namespace
{
bool OnFootOnly(s32 objective)
{
    return objective == objectives_enum::pull_driver_out_32 || objective == objectives_enum::punch_char_23 ||
           objective == objectives_enum::kill_char_on_foot_20;
}

bool IsPassenger(Car* car, Ped* ped)
{
    for (PedListNode* n = car->mPassengersList.mpFirstPed; n; n = n->mpNext)
    {
        if (n->mCharPed == ped)
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
    // the server (a client's own crimes, predicted, add points up to it: Ped::add_wanted_points)
    if (MaxWantedStars() > 1)
    {
        gPolice_7B8->mMaxWantedStars = MaxWantedStars();
    }
    // Two players getting into one empty car by the same door at once: the second one's entry ends with it as the
    // driver, without pulling the first out, which sits on in it as no driver or passenger. The
    // car doesn't know it: blown up, it doesn't kill it, and it drives the wreck without physics (a crash). Out at
    // the side, as a car-jack would have put it
    for (int i = 0; i < PlayerCount(); i++)
    {
        Ped* ped = InGamePed(i);
        Car* car = ped ? ped->mCar : NULL;
        if (car && ped->mPedState1 == ped_state_1::in_car_10 && car->mDriver != ped && !IsPassenger(car, ped))
        {
            ForceOutBesideCar(ped, car);
        }
    }
    // A ped knocked flying (its body an object, Ped::mpObj2C) whose object is gone (the snapshot's removal, or
    // the server's pickups taking its place): Char::UpdateImmobilized read the object's sprite, freed (a crash at the end
    // of a long game). Without it, the game's own way: the ped is killed where it lies
    for (Ped* p = gPedPool->mPool.mpPrev; p; p = p->mpNext)
    {
        Object* body = p->mpObj2C;
        if (body && !body->mSprite)
        {
            p->mpObj2C = NULL;
        }
    }
    // A car's AI remembered a car it hit that has no sprite now (one going), and read its position on the next tick
    // (CarAI::ManageCollisions: a crash in a resimulation)
    for (Car* c = gCar_BC_Pool->mPool.mpPrev; c; c = c->mpNext)
    {
        CarAI* ai = c->mAI;
        if (ai && ai->mCarInCollision && !ai->mCarInCollision->mCarSprite)
        {
            ai->mCarInCollision = NULL;
            ai->mBf24.bCollidedWithCar = 0; // the collision to handle (it says there is a car)
        }
    }
    // A chase task (police cars boxing in a wanted player) whose driver has no car with it now: its car went (the
    // server's) or is another (the snapshot's). CarAI::UpdateStateMachine found that driver in the table
    // (FindDriverInState) and read its car's task. Freed, and no car keeps it (as AmbulanceService does)
    std::set<Ped*> peds;
    std::set<Car*> cars;
    for (Ped* p = gPedPool->mPool.mpPrev; p; p = p->mpNext)
    {
        peds.insert(p);
    }
    for (Car* c = gCar_BC_Pool->mPool.mpPrev; c; c = c->mpNext)
    {
        cars.insert(c);
    }
    for (CarChaseTask& t : gCarChaseTaskTable->mEntries)
    {
        Ped* d = t.mpDriver;
        Car* car = d && peds.count(d) ? d->mCar : NULL;
        if (t.mInUse == 1 && !(car && cars.count(car) && car->mpChaseTask == &t))
        {
            for (Car* c : cars)
            {
                c->mpChaseTask = c->mpChaseTask == &t ? NULL : c->mpChaseTask;
            }
            gCarChaseTaskTable->FreeEntry(&t);
        }
    }
    for (Ped* p = gPedPool->mPool.mpPrev; p; p = p->mpNext)
    {
        // A ped on foot in the car state (the snapshot's: the server's got in, its entry here another) with no car to get
        // into: Ped::UpdateCharB4 seats it in its char's target car, and read none. Its own car, or on foot
        // So for getting in and out (the snapshot's states, the car not here: Char::UpdateAnimState aligned it
        // with the doors of none, CarDoorAlignmentSolver): the car it gets into, or on foot
        Char* body = p->mGameObject;
        const s32 s1 = p->mPedState1;
        if (body && (s1 == ped_state_1::in_car_10 || s1 == ped_state_1::entering_car_3 || s1 == ped_state_1::exiting_car_4) &&
            !(body->mTargetCar && cars.count(body->mTargetCar)))
        {
            if (p->mCar && cars.count(p->mCar))
            {
                body->mTargetCar = p->mCar;
            }
            else if (s1 == ped_state_1::entering_car_3 && p->mTargetToEnter && cars.count(p->mTargetToEnter))
            {
                body->mTargetCar = p->mTargetToEnter;
            }
            else
            {
                p->mPedState1 = ped_state_1::walking_0;
            }
        }
        if (p->mCar && !p->mGameObject &&
            (OnFootOnly(p->mObjective) || OnFootOnly(p->mInternalObjective)))
        {
            p->mObjective = objectives_enum::no_obj_0;
            p->mInternalObjective = objectives_enum::no_obj_0;
        }
        // A fire engine's crew told to aim its water cannon from a car without one, or at no car (here its car was
        // another than the server's, or the order came first): Ped::AimVehicleTurretStateMachine read the cannon
        const bool aiming = p->mObjective == objectives_enum::turret_put_out_car_fire_60 ||
                            p->mInternalObjective == objectives_enum::turret_put_out_car_fire_60;
        if (aiming && (!p->mTargetObjectiveCar || !p->mCar ||
                       !p->mCar->mAttachments.GetSpriteForModel(objects::water_cannon_114)))
        {
            p->mObjective = objectives_enum::no_obj_0;
            p->mInternalObjective = objectives_enum::no_obj_0;
        }
    }
}

// An arrow to each other player in the game. The game makes them when the HUD starts (Hud::Init: those in the
// game then) and drops one for good once its player is out of the game for a tick (ArrowTrace::UpdateAimCoordinates):
// here players join after the start, and come and go with the snapshots, so a missing one is made again (all of them,
// as the game makes them), before each tick, the resimulated ones too
void net::engine::KeepPlayerArrows()
{
    if (!gHud_2B00 || !gGame_0x40 || gGameSession.GetMultiplayerGamemode() == TAG_GAME_3)
    {
        return; // the tag game: an arrow to the fugitive alone (TagGame.cpp)
    }
    Hud_Arrow_7C_Array& arrows = gHud_2B00->mArrows;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player* p = gGame_0x40->mPlayers[i];
        if (!p || p->mIsUser || !p->mInUse || !p->mPlayerPed)
        {
            continue;
        }
        bool found = false;
        for (Hud_Arrow& a : arrows.mArray)
        {
            const ArrowTrace& t = a.mTargets.mPrimaryTarget;
            found = found || (a.mTargets.mGangInfo.mInUse && t.mTargetType == ArrowTargetType::Player_6 && t.mPlayer == p);
        }
        if (!found)
        {
            static unsigned made;
            if (++made <= 20 || made % 100 == 0)
            {
                printf("net: the arrows to the other players made again (%u times), player %d's was missing\n", made, i);
            }
            arrows.CreatePlayerArrows();
            return;
        }
    }
}

// What is attached to a car (a tank's turret, a fire engine's water cannon) put back on it after the snapshot moved the
// car, before the frame is drawn: the game moves an attachment with its car in the tick only, so it was drawn where the
// car had been
void net::engine::AnchorAttachments()
{
    for (Car* c = gCar_BC_Pool->mPool.mpPrev; c; c = c->mpNext)
    {
        Sprite* car = c->mCarSprite;
        for (AttachedSprite* a = car ? c->mAttachments.mP18 : NULL; a; a = a->mpNext)
        {
            Object* o = a->mpSprite ? a->mpSprite->AsObject2C() : NULL;
            if (!o || !o->mSprite || o->mRemovalState)
            {
                continue;
            }
            // As the game does: the offset turned with the car, its rotation on top of the car's
            Fix16 x = car->mXy.x, y = car->mXy.y;
            if (a->mOffsetX != Fix16(0) || a->mOffsetY != Fix16(0))
            {
                x = a->mOffsetX;
                y = a->mOffsetY;
                Ang16 rot = car->mRotation;
                Ang16::RotateVector(x, y, rot);
                x += car->mXy.x;
                y += car->mXy.y;
            }
            o->mSprite->set_xyz_lazy(x, y, car->mZpos);
            o->mSprite->set_ang_lazy(car->mRotation + a->mOffsetRot);
        }
    }
}
