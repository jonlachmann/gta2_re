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
#include "Source/GameSession.hpp"
#include "Source/Weapon.hpp"
#include <string.h>

EXTERN_GLOBAL(u32, gNetworkPlayerIdx_6F56C8);
EXTERN_GLOBAL(Network_PlayerInputs, gCurrentNetInputs_6F57D8);

namespace
{
using net::FieldDesc;

net::Schema BuildSchema()
{
    net::Schema s;
    net::EntityType player = {"player", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"in_car", 1, false}, {"buttons", 22, false},
                                         {"health", 10, true}, {"deaths", 8, false}, {"killer", 6, false}, {"weapon", 5, false},
                                         {"frags", 12, true}, {"points", 32, true}}};
    for (int i = 0; i < net::engine::kPlayerWeapons; i++)
    {
        static const char* const kAmmo[net::engine::kPlayerWeapons] = {"ammo0", "ammo1", "ammo2", "ammo3", "ammo4", "ammo5", "ammo6", "ammo7",
                                                         "ammo8", "ammo9", "ammo10", "ammo11", "ammo12", "ammo13", "ammo14"};
        player.fields.push_back(FieldDesc{kAmmo[i], 16, false});
    }
    net::EntityType car = {"car", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"model", 8, false}}};
    net::EntityType ped = {"ped", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"remap", 8, false}}};
    s.types.push_back(player); // kEntityPlayer
    s.types.push_back(car); // kEntityCar
    s.types.push_back(ped); // kEntityPed
    return s;
}

// The server's death count per player: one more each time Player::field_28_wasted_or_busted turns on
uint8_t gDeaths[MAX_PLAYERS];
uint8_t gKiller[MAX_PLAYERS];
bool gWasDead[MAX_PLAYERS];
// A client's: the deaths it has carried out, in the rollback set
struct
{
    uint8_t mDone[MAX_PLAYERS];
} gNetDeaths;

int PlayerIndexOfPed(s32 pedId)
{
    Ped* killer = pedId ? gPedManager_6787BC->PedById(pedId) : NULL;
    return killer && killer->field_15C_player ? killer->field_15C_player->field_2E_idx : -1;
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
        Ped* ped = p && p->field_8E_in_use ? p->field_2C4_player_ped : NULL; // slots out of the game aren't sent
        Sprite* s = ped ? PedSprite(ped) : NULL;
        if (!s)
        {
            continue;
        }
        EntityState e = Entity(1 + i, kEntityPlayer);
        PutPosition(e, s, kPlayerX, kPlayerY, kPlayerZ, kPlayerRot);
        e.f[kPlayerInCar] = ped->field_16C_car != NULL;
        e.f[kPlayerButtons] = (int32_t)(p->field_4_inputs & 0x3FFFFF);
        e.f[kPlayerHealth] = ped->field_216_health;
        const bool dead = p->field_28_wasted_or_busted != 0;
        if (dead && !gWasDead[i] && i < MAX_PLAYERS)
        {
            gDeaths[i]++;
            gKiller[i] = (uint8_t)(PlayerIndexOfPed(ped->field_204_killer_id) + 1);
        }
        gWasDead[i] = dead;
        e.f[kPlayerDeaths] = gDeaths[i];
        e.f[kPlayerKiller] = gKiller[i];
        e.f[kPlayerWeapon] = p->field_788_curr_weapon_idx + 1;
        e.f[kPlayerFrags] = gGameSession_67E8E0.field_490_frags_list[i];
        e.f[kPlayerPoints] = gGameSession_67E8E0.field_49C_points_list[i];
        for (int w = 0; w < kPlayerWeapons; w++)
        {
            e.f[kPlayerAmmo0 + w] = p->field_718_weapons[w] ? p->field_718_weapons[w]->field_0_ammo : 0;
        }
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

bool net::engine::PlayerOf(const EntityState& e, int* pPlayer, uint32_t* pButtons)
{
    if (e.type != kEntityPlayer)
    {
        return false;
    }
    *pPlayer = (int)e.id - 1;
    *pButtons = (uint32_t)e.f[kPlayerButtons];
    return true;
}

int net::engine::PlayerHealth(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerHealth] : 0;
}

int net::engine::PlayerFrags(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerFrags] : 0;
}

int net::engine::PlayerDeaths(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerDeaths] : 0;
}

void net::engine::Apply(const Snapshot& snap)
{
    // The slots the server has in the game, and only those (a join or a leave on the server: the same here)
    bool sent[MAX_PLAYERS] = {};
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        if (e.type == kEntityPlayer && e.id >= 1 && e.id <= MAX_PLAYERS)
        {
            sent[e.id - 1] = true;
            if (!SlotInGame((int)e.id - 1))
            {
                PutInGame((int)e.id - 1, false); // its place and score follow from the snapshot
            }
        }
    }
    for (int i = 0; i < PlayerCount() && i < MAX_PLAYERS; i++)
    {
        if (!sent[i] && SlotInGame(i))
        {
            TakeOutOfGame(i);
        }
    }
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        if (e.type != kEntityPlayer)
        {
            continue; // cars and peds: replica.cpp
        }
        Player* p = GetPlayer((int)e.id - 1);
        Ped* ped = p ? p->field_2C4_player_ped : NULL;
        if (!ped)
        {
            continue;
        }
        const int i = (int)e.id - 1;
        // Weapons and ammo are the server's
        if (e.f[kPlayerWeapon] >= 1 && e.f[kPlayerWeapon] <= 28)
        {
            p->field_788_curr_weapon_idx = (s16)(e.f[kPlayerWeapon] - 1);
        }
        for (int w = 0; w < kPlayerWeapons; w++)
        {
            if (p->field_718_weapons[w])
            {
                p->field_718_weapons[w]->field_0_ammo = (u16)e.f[kPlayerAmmo0 + w];
            }
        }
        // A death the server had and this game hasn't carried out: the game's own death, credited to the killer, where
        // this game has the ped (the server has already respawned it: the next snapshot brings it there)
        if (i < MAX_PLAYERS && (uint8_t)e.f[kPlayerDeaths] != gNetDeaths.mDone[i])
        {
            gNetDeaths.mDone[i] = (uint8_t)e.f[kPlayerDeaths];
            if (!ped->isDead_403B60() && !p->field_28_wasted_or_busted)
            {
                const int k = e.f[kPlayerKiller] - 1;
                Player* killer = GetPlayer(k);
                ped->field_204_killer_id = killer && killer->field_2C4_player_ped ? killer->field_2C4_player_ped->field_200_id : 0;
                ped->field_208_invulnerability = 0;
                ped->Kill_46F9D0();
                continue;
            }
        }
        if (!ped->isDead_403B60())
        {
            ped->set_health_4039A0((s16)e.f[kPlayerHealth]);
        }
        const Fix16 x(e.f[kPlayerX], 0), y(e.f[kPlayerY], 0), z(e.f[kPlayerZ], 0);
        const Ang16 rot((s16)e.f[kPlayerRot], (u8)0);
        if (ped->field_16C_car)
        {
            MoveCar(ped->field_16C_car, x, y, z, rot);
        }
        else
        {
            MovePed(ped, x, y, z, rot);
        }
    }
    ApplyCarsAndPeds(snap);
}

void net::engine::ApplyScores(const Snapshot& snap)
{
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        const int i = (int)e.id - 1;
        if (e.type == kEntityPlayer && i >= 0 && i < MAX_PLAYERS)
        {
            gGameSession_67E8E0.field_490_frags_list[i] = (s16)e.f[kPlayerFrags];
            gGameSession_67E8E0.field_49C_points_list[i] = e.f[kPlayerPoints];
        }
    }
}

void net::engine::NetDeaths_Memory(unsigned char** ppAddr, size_t* pSize)
{
    *ppAddr = (unsigned char*)&gNetDeaths;
    *pSize = sizeof(gNetDeaths);
}

void net::engine::DebugKill(int victim, int killer)
{
    Player* p = GetPlayer(victim);
    Player* k = GetPlayer(killer);
    Ped* ped = p ? p->field_2C4_player_ped : NULL;
    if (ped && !ped->isDead_403B60())
    {
        ped->field_204_killer_id = k && k->field_2C4_player_ped ? k->field_2C4_player_ped->field_200_id : 0;
        ped->field_208_invulnerability = 0;
        ped->Kill_46F9D0();
    }
}

bool net::engine::PlayerWasted(int player)
{
    Player* p = GetPlayer(player);
    return p && p->field_28_wasted_or_busted;
}

bool net::engine::PlayerPose(int player, float* pX, float* pY, float* pZ, int* pRot)
{
    Player* p = GetPlayer(player);
    Sprite* s = p && p->field_2C4_player_ped ? PedSprite(p->field_2C4_player_ped) : NULL;
    if (!s)
    {
        return false;
    }
    *pX = s->field_14_xy.x.ToFloat();
    *pY = s->field_14_xy.y.ToFloat();
    *pZ = s->field_1C_zpos.ToFloat();
    *pRot = s->field_0_rotation.rValue;
    return true;
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
