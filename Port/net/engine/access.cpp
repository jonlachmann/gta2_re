// Port/net (docs/netcode.md, "Net schema"): players, inputs and entities as plain data. Positions are Fix16 raw
// values (14 fraction bits): x and y below 256 blocks fit 22 bits, z below 8 blocks 17 bits; angles are Ang16
// (0..1439), 11 bits. So what a client gets is exactly the server's value.
#include "internal.hpp"
#include "Source/Car.hpp"
#include "Source/CarPhysics.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/SpriteGrid.hpp"
#include "Source/char.hpp"
#include "Source/network_data.hpp"
#include "Source/sprite.hpp"
#include <string.h>

EXTERN_GLOBAL(u32, gNetworkPlayerIdx_6F56C8);
EXTERN_GLOBAL(Network_PlayerInputs, gCurrentNetInputs_6F57D8);

namespace
{
using net::FieldDesc;

net::Schema BuildSchema()
{
    net::Schema s;
    net::EntityType player = {"player", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"in_car", 1, false}, {"buttons", 22, false}}};
    net::EntityType car = {"car", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"model", 8, false}}};
    net::EntityType ped = {"ped", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"remap", 8, false}}};
    s.types.push_back(player); // kEntityPlayer
    s.types.push_back(car); // kEntityCar
    s.types.push_back(ped); // kEntityPed
    return s;
}

Player* GetPlayer(int i)
{
    Game* g = gGame_0x40_67E008;
    return g && i >= 0 && i < (int)g->field_23_num_players ? g->field_4_players[i] : NULL;
}

net::EntityState Entity(uint32_t id, uint8_t type)
{
    net::EntityState e;
    memset(&e, 0, sizeof(e));
    e.id = id;
    e.type = type;
    return e;
}

void PutPosition(net::EntityState& e, const Sprite* s, int x, int y, int z, int rot)
{
    e.f[x] = s->field_14_xy.x.mValue;
    e.f[y] = s->field_14_xy.y.mValue;
    e.f[z] = s->field_1C_zpos.mValue;
    e.f[rot] = s->field_0_rotation.rValue;
}

} // namespace

Sprite* net::engine::PedSprite(Ped* ped)
{
    if (ped->field_16C_car)
    {
        return ped->field_16C_car->field_50_car_sprite;
    }
    return ped->field_168_game_object ? ped->field_168_game_object->field_80_sprite_ptr : NULL;
}

// Moves a car the way Car::SetPosition_443D00 does, and turns it
void net::engine::MoveCar(Car* car, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    car->SetPosition_443D00(x, y, z);
    car->field_50_car_sprite->set_ang_lazy_420690(rot);
    if (car->field_58_physics)
    {
        car->field_58_physics->SetSprite_563560(car->field_50_car_sprite);
    }
}

// Moves a ped on foot: its sprite out of the collision grid and back, as Car::SetPosition_443D00 does for cars
void net::engine::MovePed(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    Sprite* s = ped->field_168_game_object ? ped->field_168_game_object->field_80_sprite_ptr : NULL;
    if (!s)
    {
        return;
    }
    gSpriteGrid_1_679208->RemoveFromRegionBuckets_477B60(s);
    s->set_xyz_lazy_420600(x, y, z);
    gSpriteGrid_1_679208->AddToRegionBuckets_477B20(s);
    ped->SetRotation_433C00(rot);
    ped->field_1AC_cam.x = x;
    ped->field_1AC_cam.y = y;
    ped->field_1AC_cam.z = z;
}
const net::Schema& net::engine::NetSchema()
{
    static const Schema s = BuildSchema();
    return s;
}

bool net::engine::GameRunning()
{
    return gGame_0x40_67E008 && gGame_0x40_67E008->field_23_num_players > 0;
}

int net::engine::PlayerCount()
{
    return gGame_0x40_67E008 ? gGame_0x40_67E008->field_23_num_players : 0;
}

int net::engine::LocalPlayer()
{
    return (int)gNetworkPlayerIdx_6F56C8;
}

uint32_t net::engine::LocalInput()
{
    return gCurrentNetInputs_6F57D8.field_0_inputs[gNetworkPlayerIdx_6F56C8].field_0_Inputs;
}

void net::engine::SetPlayerInput(int player, uint32_t buttons)
{
    if (player >= 0 && player < MAX_PLAYERS)
    {
        gCurrentNetInputs_6F57D8.field_0_inputs[player].field_0_Inputs = buttons;
    }
}

void net::engine::SetTickMs(int ms)
{
    if (gGame_0x40_67E008 && ms > 0)
    {
        gGame_0x40_67E008->field_34_network_frame_ms = ms; // Game::GetFrameDurationMs_4B8BB0 in network games
    }
}

bool net::engine::PlayerHasPed(int player)
{
    Player* p = GetPlayer(player);
    return p && p->field_2C4_player_ped;
}

bool net::engine::PlayerPosition(int player, float* pX, float* pY)
{
    Player* p = GetPlayer(player);
    Sprite* s = p && p->field_2C4_player_ped ? PedSprite(p->field_2C4_player_ped) : NULL;
    if (!s)
    {
        return false;
    }
    *pX = s->field_14_xy.x.ToFloat();
    *pY = s->field_14_xy.y.ToFloat();
    return true;
}

void net::engine::Capture(Snapshot& out)
{
    out.entities.clear();
    for (int i = 0; i < PlayerCount(); i++)
    {
        Player* p = GetPlayer(i);
        Ped* ped = p ? p->field_2C4_player_ped : NULL;
        Sprite* s = ped ? PedSprite(ped) : NULL;
        if (!s)
        {
            continue;
        }
        EntityState e = Entity(1 + i, kEntityPlayer);
        PutPosition(e, s, kPlayerX, kPlayerY, kPlayerZ, kPlayerRot);
        e.f[kPlayerInCar] = ped->field_16C_car != NULL;
        e.f[kPlayerButtons] = (int32_t)(p->field_4_inputs & 0x3FFFFF);
        out.entities.push_back(e);
    }
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        if (c->field_50_car_sprite)
        {
            EntityState e = Entity(kCarIdBase + (uint32_t)c->field_6C_car_id, kEntityCar);
            PutPosition(e, c->field_50_car_sprite, kCarX, kCarY, kCarZ, kCarRot);
            e.f[kCarModel] = c->field_84_car_info_idx;
            out.entities.push_back(e);
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        Sprite* s = p->field_168_game_object ? p->field_168_game_object->field_80_sprite_ptr : NULL;
        if (s && !p->field_16C_car && p->field_238_ped_type != ped_type::player_2) // players are their own entities
        {
            EntityState e = Entity(kPedIdBase + p->field_200_id, kEntityPed);
            PutPosition(e, s, kPedX, kPedY, kPedZ, kPedRot);
            e.f[kPedRemap] = (uint8_t)p->field_244_remap;
            out.entities.push_back(e);
        }
    }
    SortById(out);
}

void net::engine::Apply(const Snapshot& snap, int localPlayer)
{
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        if (e.type != kEntityPlayer)
        {
            continue; // cars and peds: replica.cpp
        }
        const int i = (int)e.id - 1;
        Player* p = GetPlayer(i);
        Ped* ped = p ? p->field_2C4_player_ped : NULL;
        if (!ped)
        {
            continue;
        }
        const Fix16 x(e.f[kPlayerX], 0), y(e.f[kPlayerY], 0), z(e.f[kPlayerZ], 0);
        const Ang16 rot((s16)e.f[kPlayerRot], (u8)0);
        Sprite* s = PedSprite(ped);
        if (i == localPlayer && s)
        {
            // No prediction yet (N4): the local player runs free and is only snapped back when it is off by more
            // than a block from the server's
            const Fix16 dx = s->field_14_xy.x - x, dy = s->field_14_xy.y - y;
            if (Fix16::Abs(dx) < Fix16(1) && Fix16::Abs(dy) < Fix16(1))
            {
                continue;
            }
        }
        if (ped->field_16C_car)
        {
            MoveCar(ped->field_16C_car, x, y, z, rot);
        }
        else
        {
            MovePed(ped, x, y, z, rot);
        }
        if (i != localPlayer)
        {
            SetPlayerInput(i, (uint32_t)e.f[kPlayerButtons]); // runs it on with its own input until the next snapshot
        }
    }
    ApplyCarsAndPeds(snap);
}
