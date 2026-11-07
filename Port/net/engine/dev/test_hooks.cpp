// Port/net engine dev (docs/netcode.md, "Checks"): a server's test cheats, for the checks that need a death, a weapon, a
// wanted level, a power-up or a car at a given tick (GTA2_NET_TEST*, run by the server's test hooks, Port/net/dev).
#include "../internal.hpp"
#include "dev.hpp"
#include "Port/rand/owner.hpp"
#include "Port/port_hooks.hpp"
#include "Port/rand/state.hpp"
#include "Source/Camera.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/GameSession.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/TrafficLights.hpp"
#include "Source/Weapon.hpp"
#include "Source/char.hpp"
#include "Source/network_data.hpp"
#include "Source/rng.hpp"
#include "Source/sprite.hpp"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string>

void net::engine::DebugKill(int victim, int killer)
{
    Player* p = GetPlayer(victim);
    Ped* ped = p ? p->field_2C4_player_ped : NULL;
    if (ped && !ped->isDead_403B60())
    {
        KillCredited(ped, killer);
    }
}

void net::engine::DebugWanted(int player, int points)
{
    Player* p = GetPlayer(player);
    Ped* ped = p ? p->field_2C4_player_ped : NULL;
    if (ped && !ped->isDead_403B60() && ped->field_20A_wanted_points < points)
    {
        ped->field_20A_wanted_points = (s16)points;
    }
}

void net::engine::DebugPowerUp(int player, int powerUp)
{
    Player* p = GetPlayer(player);
    if (p && p->field_2C4_player_ped && powerUp >= 0 && powerUp < kPowerUps)
    {
        p->CollectPowerUp_564D60(powerUp);
    }
}

void net::engine::DebugCar(int player, int model)
{
    Player* p = GetPlayer(player);
    Ped* ped = p ? p->field_2C4_player_ped : NULL;
    if (ped && !ped->field_16C_car && gCarManager_6C_677930)
    {
        // Beside the player, as the debug menu spawns one (ImGuiDebug.cpp): scale 1, counted as recycled
        Fix16 scale;
        scale.mValue = 0x4000;
        Fix16 off;
        off.FromInt(2);
        Car* car = gCarManager_6C_677930->SpawnCarAt_446230(ped->get_cam_x() + off, ped->get_cam_y(), ped->get_cam_z(), 0, model, scale);
        if (car)
        {
            car->IncrementCarStats_443D70(car_kind::recycled_1);
        }
    }
}

void net::engine::DebugArm(int player, int weapon, int ammo)
{
    Player* p = GetPlayer(player);
    if (p && p->field_2C4_player_ped && weapon >= 0 && weapon < kPlayerWeapons && p->field_718_weapons[weapon] &&
        p->field_718_weapons[weapon]->field_0_ammo == 0)
    {
        p->AddWeaponWithAmmo_564960(weapon, (u8)ammo);
    }
}
