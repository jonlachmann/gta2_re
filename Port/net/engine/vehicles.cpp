// Port/net (docs/netcode.md, "Players in cars"): a client predicts getting into and out of cars by its own input;
// when the server has a ped somewhere else (another car, or on foot), these put it there at once, ending the way
// the game's own entering and leaving end:
// - into a car: as Ped.cpp's UpdateCharB4 path once the entering animation is done (the char goes back to its
//   pool, the ped is in_car_10/driving, Car::AssignDriver_4406E0);
// - out of a car: as the script's WARP_FROM_CAR_TO_POINT (Car::ClearDriver_4407F0, Ped::StartPedWalking_470200).
#include "internal.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Ped.hpp"
#include "Source/char.hpp"
#include "Source/sprite.hpp"

bool net::engine::InCarTransition(Ped* ped)
{
    const s32 s1 = ped->field_278_ped_state_1, s2 = ped->field_27C_ped_state_2, objective = ped->get_objective_403A80();
    return s1 == ped_state_1::entering_car_3 || s1 == ped_state_1::exiting_car_4 || s2 == ped_state_2::ped2_entering_a_car_6 ||
           s2 == ped_state_2::ped2_getting_out_a_car_7 || objective == objectives_enum::enter_car_as_driver_35 ||
           objective == objectives_enum::leave_car_36 || objective == objectives_enum::enter_train_37 || objective == objectives_enum::leave_train_38;
}

bool net::engine::AtCarDoor(Ped* ped)
{
    const s32 s1 = ped->field_278_ped_state_1, s2 = ped->field_27C_ped_state_2;
    return s1 == ped_state_1::entering_car_3 || s1 == ped_state_1::exiting_car_4 || s2 == ped_state_2::ped2_entering_a_car_6 ||
           s2 == ped_state_2::ped2_getting_out_a_car_7;
}

bool net::engine::LeavingCar(Ped* ped)
{
    const s32 objective = ped->get_objective_403A80();
    return ped->field_278_ped_state_1 == ped_state_1::exiting_car_4 || ped->field_27C_ped_state_2 == ped_state_2::ped2_getting_out_a_car_7 ||
           objective == objectives_enum::leave_car_36 || objective == objectives_enum::leave_train_38;
}

void net::engine::ForceOutOfCar(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    Car* car = ped->field_16C_car;
    if (!car || IsSpareDriver(ped))
    {
        return;
    }
    ped->SetObjective(objectives_enum::no_obj_0, 9999); // whatever it was about to do with the car
    ped->SetObjective2_463830(objectives_enum::no_obj_0);
    if (car->field_54_driver == ped)
    {
        car->ClearDriver_4407F0();
    }
    ped->StartPedWalking_470200(x, y, z); // a char again, walking
    MovePed(ped, x, y, z, rot); // turned the grid-safe way (out of the collision grid and back: harmless if not in it)
}

void net::engine::ForceIntoCar(Ped* ped, Car* car)
{
    if (!car || !car->field_50_car_sprite || (ped->field_16C_car == car && ped->field_278_ped_state_1 == ped_state_1::in_car_10))
    {
        return; // no car to get into (one being despawned has no sprite)
    }
    // Anyone else on the way into this car: still walking up, the game's own cancel; at the door already, this waits
    // for a later snapshot (it would finish getting in, the car's driver now, after this ped: Ped::UpdateCharB4_462B80)
    for (Ped* other = gPedPool_6787B8->field_0_pool.field_4_pPrev; other; other = other->mpNext)
    {
        Char* oc = other->field_168_game_object;
        if (other == ped || other->field_16C_car || !oc ||
            (other->field_150_target_objective_car != car && other->field_154_target_to_enter != car && oc->field_84_target_car != car))
        {
            continue;
        }
        if (other->field_27C_ped_state_2 == ped_state_2::ped2_entering_a_car_6 || other->field_278_ped_state_1 == ped_state_1::entering_car_3)
        {
            return;
        }
        other->CancelEnterCarObjective_45C5C0();
    }
    Sprite* cs = car->field_50_car_sprite;
    if (ped->field_16C_car && ped->field_16C_car != car)
    {
        Sprite* was = ped->field_16C_car->field_50_car_sprite; // out where it sits
        Sprite* at = was ? was : cs;
        ForceOutOfCar(ped, at->field_14_xy.x, at->field_14_xy.y, at->field_1C_zpos, at->field_0_rotation);
    }
    if (Ped* driver = car->field_54_driver)
    {
        if (IsSpareDriver(driver))
        {
            car->SetDriver(NULL); // the traffic driver every car shares: it isn't anywhere to get out of
        }
        else if (driver != ped)
        {
            ForceOutOfCar(driver, cs->field_14_xy.x + Fix16(1), cs->field_14_xy.y, cs->field_1C_zpos, cs->field_0_rotation);
        }
    }
    if (Char* c = ped->field_168_game_object)
    {
        if (c->field_88_obj_2c.field_0_p18)
        {
            c->field_88_obj_2c.DestroyAllSprites_5A7010();
        }
        gChar_B4_Pool_6FDB44->DeAllocate(c);
        ped->field_168_game_object = NULL;
    }
    ped->field_16C_car = car;
    ped->field_248_enter_car_as_passenger = 0;
    ped->field_278_ped_state_1 = ped_state_1::in_car_10;
    ped->field_27C_ped_state_2 = ped_state_2::ped2_driving_10;
    // Nothing left to do on foot: both its objective and the internal one (a walk to a point would run on without
    // a char, Ped::WalkToTargetPoint_46C770)
    ped->SetObjective(objectives_enum::no_obj_0, 9999);
    ped->SetObjective2_463830(objectives_enum::no_obj_0);
    car->field_88_despawn_status = car_despawn_status::active_1; // as Ped::SpawnPedInCar_45C730: not removed with a driver
    car->AssignDriver_4406E0(ped);
}
