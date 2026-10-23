// Port/net engine internals: shared by access.cpp (players, capture) and replica.cpp (a client's cars and peds).
// Decomp types: only for engine/.
#pragma once
#include "engine.hpp"

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
};
enum CarField
{
    kCarX,
    kCarY,
    kCarZ,
    kCarRot,
    kCarModel,
};
enum PedField
{
    kPedX,
    kPedY,
    kPedZ,
    kPedRot,
    kPedRemap,
};

const uint32_t kCarIdBase = 0x01000000; // + Car::field_6C_car_id
const uint32_t kPedIdBase = 0x02000000; // + Ped::field_200_id

Sprite* PedSprite(Ped* ped); // its car's when it is in one
void MoveCar(Car* car, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot);
void MovePed(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot);

// replica.cpp: a snapshot's cars and peds into a client's game (players: access.cpp)
void ApplyCarsAndPeds(const Snapshot& snap);
} // namespace engine
} // namespace net
