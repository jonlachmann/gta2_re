// Port/net engine internals: shared by access.cpp (players, capture) and replica.cpp (a client's cars and peds).
// Decomp types: only for engine/.
#pragma once
#include "engine.hpp"
#include <stddef.h>

class Car;
class Ped;
class Sprite;
class Fix16;
class Ang16;

namespace net
{
namespace engine
{
// The schema's fields (access.cpp builds the schema in this order)
enum PlayerField
{
    kPlayerX,
    kPlayerY,
    kPlayerZ,
    kPlayerRot,
    kPlayerInCar,
    kPlayerButtons,
    kPlayerHealth,
    kPlayerDeaths, // how often the server's player died (wraps at 256)
    kPlayerKiller, // who killed it last: player index + 1, 0 nobody
    kPlayerWeapon, // Player::field_788_curr_weapon_idx + 1 (0: none)
    kPlayerFrags,
    kPlayerPoints,
    kPlayerAmmo0, // field_718_weapons[0..14]: the player's own weapons' ammo
    kPlayerCar = kPlayerAmmo0 + 15, // the car it drives: Car::field_6C_car_id + 1, 0 none
    kPlayerState, // 1: getting into or out of a car (InCarTransition), 2: dead
    kPlayerWeapons = 15,
};
enum CarField
{
    kCarX,
    kCarY,
    kCarZ,
    kCarRot,
    kCarModel,
    kCarDriver, // kDriverNone, kDriverTraffic (the car manager's shared spare driver), kDriverPed (sent as a ped entity)
};
enum CarDriver
{
    kDriverNone = 0,
    kDriverTraffic = 1,
    kDriverPed = 2,
};
enum PedField
{
    kPedX,
    kPedY,
    kPedZ,
    kPedRot,
    kPedRemap,
    kPedCar, // the car it drives: Car::field_6C_car_id + 1, 0 on foot (its position is the car's then)
    kPedState, // 1: getting into or out of a car
};

const uint32_t kCarIdBase = 0x01000000; // + Car::field_6C_car_id
const uint32_t kPedIdBase = 0x02000000; // + Ped::field_200_id

Sprite* PedSprite(Ped* ped); // its car's when it is in one
// The one dummy ped every traffic car shares as its driver (Car::SpawnDriverPed, CarManager::field_4_spare_driver): no
// ped of its own, never sent, moved, forced or removed
bool IsSpareDriver(const Ped* ped);
void MoveCar(Car* car, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot);
void MovePed(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot);

// The deaths a client has carried out per player (part of the rollback set: world.cpp)
void NetDeaths_Memory(unsigned char** ppAddr, size_t* pSize);
// Player peds take no damage on a client: their health and deaths are the server's (client_mode.cpp)
void ProtectPlayers();

// vehicles.cpp: a ped into or out of a car at once, ending as the game's own entering and leaving do
bool InCarTransition(Ped* ped); // on its way into or out of a car: states, or an objective to do it
void ForceIntoCar(Ped* ped, Car* car);
void ForceOutOfCar(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot);

// replica.cpp: a snapshot's cars and peds into a client's game (players: access.cpp)
void ApplyCarsAndPeds(const Snapshot& snap);
Car* ReplicaCar(uint32_t netId); // this game's car for a server car, if it has one
bool DebugGridOk(const char* where, Car* car); // GTA2_NET_CHECKGRID: the collision grid has no freed sprites
// The replica mapping, saved and restored with the world (rollback.cpp): an opaque copy
struct ReplicaState;
ReplicaState* SaveReplicas(ReplicaState* reuse);
void RestoreReplicas(const ReplicaState* state);
void FreeReplicas(ReplicaState* state);
} // namespace engine
} // namespace net
