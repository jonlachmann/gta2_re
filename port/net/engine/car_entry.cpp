// port/net (docs/netcode.md, "The client"): a client predicts getting into and out of cars by its own input;
// when the server has a ped somewhere else (another car, or on foot), these put it there at once, ending the way
// the game's own entering and leaving end:
// - into a car: as Ped.cpp's UpdateCharB4 path once the entering animation is done (the char goes back to its
//   pool, the ped is in_car_10/driving, Car::AssignDriver);
// - out of a car: as the script's WARP_FROM_CAR_TO_POINT (Car::ClearDriver, Ped::StartPedWalking).
// Or it starts getting in or out the way the enter key starts it.
#include "internal.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Ped.hpp"
#include "Source/char.hpp"
#include "Source/sprite.hpp"

// An order to get out counts in a car only, one to get in on foot only: a SWAT van's driver that had got out here,
// the server's staying in, kept its order to leave, and was never put back (its van driverless here for a minute)
bool net::engine::InCarTransition(Ped* ped)
{
    const s32 s1 = ped->mPedState1, s2 = ped->mPedState2, objective = ped->get_objective();
    const bool inCar = ped->mCar != NULL;
    return s1 == ped_state_1::entering_car_3 || s1 == ped_state_1::exiting_car_4 || s2 == ped_state_2::ped2_entering_a_car_6 ||
           s2 == ped_state_2::ped2_getting_out_a_car_7 ||
           (!inCar && (objective == objectives_enum::enter_car_as_driver_35 || objective == objectives_enum::enter_train_37)) ||
           (inCar && (objective == objectives_enum::leave_car_36 || objective == objectives_enum::leave_train_38));
}

// entering_car_3 with ped2_following_a_car_4 is still walking: after the car while it moves, or up to it (Ped.cpp's
// enter-car state machine); at the door it is ped2_entering_a_car_6. Corrected there, the walk no longer jumped half a
// block when it reached the door
bool net::engine::AtCarDoor(Ped* ped)
{
    const s32 s1 = ped->mPedState1, s2 = ped->mPedState2;
    return (s1 == ped_state_1::entering_car_3 && s2 != ped_state_2::ped2_following_a_car_4) || s1 == ped_state_1::exiting_car_4 ||
           s2 == ped_state_2::ped2_entering_a_car_6 || s2 == ped_state_2::ped2_getting_out_a_car_7;
}

bool net::engine::LeavingCar(Ped* ped)
{
    const s32 objective = ped->get_objective();
    return ped->mPedState1 == ped_state_1::exiting_car_4 || ped->mPedState2 == ped_state_2::ped2_getting_out_a_car_7 ||
           objective == objectives_enum::leave_car_36 || objective == objectives_enum::leave_train_38;
}

void net::engine::ForceOutOfCar(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    Car* car = ped->mCar;
    if (!car || IsSpareDriver(ped))
    {
        return;
    }
    ped->SetObjective(objectives_enum::no_obj_0, 9999); // whatever it was about to do with the car
    ped->SetObjective2(objectives_enum::no_obj_0);
    if (car->mDriver == ped)
    {
        car->ClearDriver();
    }
    if (ped->mEnterCarAsPassenger)
    {
        car->mPassengersList.RemovePed_471240(ped); // as the game lets a passenger out (Ped.cpp)
        ped->mEnterCarAsPassenger = 0;
    }
    ped->StartPedWalking(x, y, z); // a char again, walking
    MovePed(ped, x, y, z, rot); // turned the grid-safe way (out of the collision grid and back: harmless if not in it)
}

void net::engine::ForceOutBesideCar(Ped* ped, Car* car)
{
    if (Sprite* s = car ? car->mCarSprite : NULL)
    {
        ForceOutOfCar(ped, s->mXy.x + Fix16(1), s->mXy.y, s->mZpos, s->mRotation);
    }
}

// As the enter key starts it (Player::HandleControls): the game walks the ped to the door and in
void net::engine::StartEntering(Ped* ped, Car* car)
{
    ped->SetObjective2(objectives_enum::no_obj_0);
    ped->SetObjective(car->IsTrainModel() ? objectives_enum::enter_train_37 : objectives_enum::enter_car_as_driver_35);
    ped->SetTargetObjectiveCar(car);
    ped->set_enter_car_as_passenger(0);
    ped->set_target_car_door(0);
}

void net::engine::StartLeaving(Ped* ped)
{
    Car* car = ped->mCar;
    ped->SetObjective2(objectives_enum::no_obj_0);
    ped->SetObjective(car->mCarInfoIdx == car_model_enum::TRAIN ? objectives_enum::leave_train_38 : objectives_enum::leave_car_36);
    ped->SetTargetObjectiveCar(car);
}

void net::engine::ForceIntoCar(Ped* ped, Car* car)
{
    if (!car || !car->mCarSprite || (ped->mCar == car && ped->mPedState1 == ped_state_1::in_car_10))
    {
        return; // no car to get into (one being despawned has no sprite)
    }
    // Anyone else on the way into this car: still walking up, the game's own cancel; at the door already, this waits
    // for a later snapshot (it would finish getting in, the car's driver now, after this ped: Ped::UpdateCharB4)
    for (Ped* other = gPedPool->mPool.mpPrev; other; other = other->mpNext)
    {
        Char* oc = other->mGameObject;
        if (other == ped || other->mCar || !oc ||
            (other->mTargetObjectiveCar != car && other->mTargetToEnter != car && oc->mTargetCar != car))
        {
            continue;
        }
        if (other->mPedState2 == ped_state_2::ped2_entering_a_car_6 || other->mPedState1 == ped_state_1::entering_car_3)
        {
            return;
        }
        other->CancelEnterCarObjective();
    }
    Sprite* cs = car->mCarSprite;
    if (ped->mCar && ped->mCar != car)
    {
        Sprite* was = ped->mCar->mCarSprite; // out where it sits
        Sprite* at = was ? was : cs;
        ForceOutOfCar(ped, at->mXy.x, at->mXy.y, at->mZpos, at->mRotation);
    }
    if (Ped* driver = car->mDriver)
    {
        if (IsSpareDriver(driver))
        {
            car->SetDriver(NULL); // the traffic driver every car shares: it isn't anywhere to get out of
        }
        else if (driver != ped)
        {
            ForceOutBesideCar(driver, car);
        }
    }
    if (Char* c = ped->mGameObject)
    {
        if (c->mObj2c.mP18)
        {
            c->mObj2c.DestroyAllSprites();
        }
        gChar_B4_Pool->DeAllocate(c);
        ped->mGameObject = NULL;
    }
    ped->mCar = car;
    ped->mEnterCarAsPassenger = 0;
    // Nothing left to do on foot: both its objective and the internal one (a walk to a point would run on without
    // a char, Ped::WalkToTargetPoint). Before the states: no objective is a walk (ChangePedStatesByMode),
    // and after them a ped sat in its car walking, its AI never driving again (a police car's crew)
    ped->SetObjective(objectives_enum::no_obj_0, 9999);
    ped->SetObjective2(objectives_enum::no_obj_0);
    ped->mPedState1 = ped_state_1::in_car_10;
    ped->mPedState2 = ped_state_2::ped2_driving_10;
    car->mDespawnStatus = car_despawn_status::active_1; // as Ped::SpawnPedInCar: not removed with a driver
    car->AssignDriver(ped);
}

bool net::engine::IsPassengerOf(Ped* ped, Car* car)
{
    return ped->mCar == car && ped->mEnterCarAsPassenger && ped->mPedState1 == ped_state_1::in_car_10;
}

// A passenger as the game seats one (Ped::EnterCarAsPassenger: in the passenger list, no char): the server's
// (an instant gang in its leader's car, a bus's riders), which the snapshots now carry
void net::engine::ForceIntoCarAsPassenger(Ped* ped, Car* car)
{
    if (!car || !car->mCarSprite || IsPassengerOf(ped, car))
    {
        return;
    }
    if (ped->mCar)
    {
        ForceOutBesideCar(ped, ped->mCar); // in another car, or this one's driver
    }
    if (Char* c = ped->mGameObject)
    {
        if (c->mObj2c.mP18)
        {
            c->mObj2c.DestroyAllSprites();
        }
        gChar_B4_Pool->DeAllocate(c);
        ped->mGameObject = NULL;
    }
    ped->SetObjective(objectives_enum::no_obj_0, 9999); // as ForceIntoCar: nothing left to do on foot
    ped->SetObjective2(objectives_enum::no_obj_0);
    ped->EnterCarAsPassenger(car);
}
