// port/net engine dev (port/docs/netcode.md, "Checks"): a server's test cheats, for the checks that need a death, a weapon, a
// wanted level, a power-up or a car at a given tick (GTA2_NET_TEST*, run by the server's test hooks, port/net/dev).
#include "../internal.hpp"
#include "dev.hpp"
#include "Source/Car.hpp"
#include "Source/Game.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/Weapon.hpp"
#include <stdlib.h>
#include <string>

void net::engine::DebugKill(int victim, int killer)
{
    Player* p = GetPlayer(victim);
    Ped* ped = p ? p->mPlayerPed : NULL;
    if (ped && !ped->isDead())
    {
        KillCredited(ped, killer);
    }
}

void net::engine::DebugWanted(int player, int points)
{
    Player* p = GetPlayer(player);
    Ped* ped = p ? p->mPlayerPed : NULL;
    if (ped && !ped->isDead() && ped->mWantedPoints < points)
    {
        ped->mWantedPoints = (s16)points;
    }
}

void net::engine::DebugPowerUp(int player, int powerUp)
{
    Player* p = GetPlayer(player);
    if (p && p->mPlayerPed && powerUp >= 0 && powerUp < kPowerUps)
    {
        p->CollectPowerUp(powerUp);
    }
}

void net::engine::DebugCar(int player, int model)
{
    Player* p = GetPlayer(player);
    Ped* ped = p ? p->mPlayerPed : NULL;
    if (ped && !ped->mCar && gCarManager_6C)
    {
        // Beside the player, as the debug menu spawns one (ImGuiDebug.cpp): scale 1, counted as recycled
        Fix16 scale;
        scale.mValue = 0x4000;
        Fix16 off;
        off.FromInt(2);
        Car* car = gCarManager_6C->SpawnCarAt(ped->get_cam_x() + off, ped->get_cam_y(), ped->get_cam_z(), 0, model, scale);
        if (car)
        {
            car->IncrementCarStats(car_kind::recycled_1);
        }
    }
}

void net::engine::DebugArm(int player, int weapon, int ammo)
{
    Player* p = GetPlayer(player);
    if (p && p->mPlayerPed && weapon >= 0 && weapon < kPlayerWeapons && p->mWeapons[weapon] &&
        p->mWeapons[weapon]->mAmmo == 0)
    {
        p->AddWeaponWithAmmo(weapon, (u8)ammo);
    }
}
