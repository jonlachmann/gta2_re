// Port/net (docs/netcode.md, "Net schema"): players, inputs and entities as plain data. Positions are Fix16 raw
// values (14 fraction bits): x and y below 256 blocks fit 22 bits, z below 8 blocks 17 bits; angles are Ang16
// (0..1439), 11 bits. So what a client gets is exactly the server's value.
#include "internal.hpp"
#include "Source/Camera.hpp"
#include "Source/Car.hpp"
#include "Source/gbh_graphics.hpp"
#include "Source/CarPhysics.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/SpriteGrid.hpp"
#include "Source/char.hpp"
#include "Source/rng.hpp"
#include "Source/network_data.hpp"
#include "Source/sprite.hpp"
#include "Source/GameSession.hpp"
#include "Source/Weapon.hpp"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>

EXTERN_GLOBAL(u32, gNetworkPlayerIdx_6F56C8);
EXTERN_GLOBAL(Network_PlayerInputs, gCurrentNetInputs_6F57D8);

namespace
{
using net::FieldDesc;

// A car's whole physics state on the wire: every CarPhysics member but its two pointers and the unused padding, at
// its own size (by name: the 64-bit layout isn't the original's). Whatever acts on a car (input, collisions, slopes,
// oil, skids) leaves its mark here, so a predicted car goes on from exactly the server's.
struct PhysicsMember
{
    const char* name;
    size_t offset;
    uint8_t size; // bytes: 1, 2 or 4
    bool isSigned;
};
#define PM(member, sz, sg) {#member, offsetof(CarPhysics, member), sz, sg}
#define PM_POINT(member) PM(member.x, 4, true), PM(member.y, 4, true)
const PhysicsMember kPhysics[] = {
    PM_POINT(field_0_vel_read_only), PM(field_8_total_damage_q, 4, false), PM_POINT(field_10_last_skid_pos[0]),
    PM_POINT(field_10_last_skid_pos[1]), PM_POINT(field_10_last_skid_pos[2]), PM_POINT(field_10_last_skid_pos[3]),
    PM_POINT(field_30_cm1), PM_POINT(field_38_cp1), PM_POINT(field_40_linvel_1), PM_POINT(field_48_force_accum),
    PM_POINT(field_50_linear_accel), PM(field_58_theta, 2, true), PM(field_60_gas_pedal, 4, true),
    PM(field_64_brake_pressure, 4, true), PM(field_68_air_z_vel, 4, true), PM(field_6C_cp3, 4, true), PM(field_70_z_vel, 4, true),
    PM(field_74_ang_vel_rad, 4, true), PM(field_78_pointing_ang_rad, 4, true), PM(field_7C_torque_accum, 4, true),
    PM(field_80_angular_accel, 4, true), PM(field_84_front_skid, 4, true), PM(field_88_rear_skid, 4, true), PM(field_8C_state, 4, true),
    PM(field_90_timer_since_last_move, 1, false), PM(field_91_is_foot_brake_on, 1, true), PM(field_92_is_hand_brake_on, 1, true),
    PM(field_93_is_forward_gas_on, 1, true), PM(field_94_is_backward_gas_on, 1, true), PM(field_95_is_coasting, 1, true),
    PM(field_98_surface_type, 4, true), PM(field_9C_block_spec, 4, true), PM(field_A0_oil_spin_dir, 4, true),
    PM(field_A4_oil_spin_timer, 1, true), PM(field_A5_current_slope_length, 1, false), PM(field_A6_current_slope_left_tiles, 1, true),
    PM(field_A7_current_tile_z, 1, false), PM(field_A8_hand_brake_force, 1, true), PM(field_A9_car_model, 1, true),
    PM(field_AA_sbw, 1, true), PM(field_AB_tpa, 1, true), PM(field_AC_drive_wheels_locked_q, 1, false),
    PM(field_AD_turn_direction, 1, true)};
#undef PM_POINT
#undef PM
const size_t kCp1X = 13; // kPhysics' field_38_cp1.x: 0 only for a car without physics (nobody is at x 0)
static_assert(net::engine::kCarPhysics + sizeof(kPhysics) / sizeof(kPhysics[0]) <= net::kMaxFields, "a car's fields");

int32_t GetMember(const CarPhysics* m, const PhysicsMember& d)
{
    const unsigned char* p = (const unsigned char*)m + d.offset;
    if (d.size == 1)
    {
        return d.isSigned ? (int32_t) * (const int8_t*)p : (int32_t) * (const uint8_t*)p;
    }
    if (d.size == 2)
    {
        int16_t v;
        memcpy(&v, p, 2);
        return v;
    }
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

void SetMember(CarPhysics* m, const PhysicsMember& d, int32_t v)
{
    unsigned char* p = (unsigned char*)m + d.offset;
    if (d.size == 1)
    {
        *p = (unsigned char)v;
    }
    else if (d.size == 2)
    {
        const int16_t s = (int16_t)v;
        memcpy(p, &s, 2);
    }
    else
    {
        memcpy(p, &v, 4);
    }
}

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
    player.fields.push_back(FieldDesc{"car", 25, false}); // kPlayerCar
    player.fields.push_back(FieldDesc{"state", 2, false}); // kPlayerState
    net::EntityType car = {"car", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"model", 8, false},
                                   {"driver", 2, false}}};
    for (size_t i = 0; i < sizeof(kPhysics) / sizeof(kPhysics[0]); i++)
    {
        car.fields.push_back(FieldDesc{kPhysics[i].name, (uint8_t)(kPhysics[i].size * 8), kPhysics[i].isSigned});
    }
    net::EntityType ped = {"ped", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"remap", 8, false},
                                   {"car", 25, false}, {"state", 1, false}}};
    s.types.push_back(player); // kEntityPlayer
    s.types.push_back(car); // kEntityCar
    s.types.push_back(ped); // kEntityPed
    net::EntityType world = {"world", {{"cycle", 32, false}}};
    s.types.push_back(world); // kEntityWorld
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
// A car or a ped moved and turned at once. The collision grid keeps a sprite in the rows of its collision rect as it
// was added (SpriteGrid::RemoveFromRegionBuckets_477B60 takes it out of the rows of the stored rect, and
// AddToRegionBuckets_477B20 computes a new one): so position and rotation both change between the two. Turning a
// sprite while it is in the grid, as Car::SetPosition_443D00 followed by a turn did, left it in rows its next
// removal missed; once it was freed and reused (a roof light, a particle), a collision query crashed on it.
// Only a car in the collision grid is moved: one that is going away (despawning_5, despawned_6: its last update
// left it out of the grid, and freeing it doesn't look there) or carried by something (deactivated_7: in grid 3,
// Car::Deactivate_43AA60, its carrier moves it) would be put into the grid by the move and left there when freed.
void net::engine::MoveCar(Car* car, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    const s32 status = car->field_88_despawn_status;
    if (status == car_despawn_status::despawning_5 || status == car_despawn_status::despawned_6 || status == car_despawn_status::deactivated_7)
    {
        return;
    }
    Sprite* s = car->field_50_car_sprite;
    gSpriteGrid_1_679208->RemoveFromRegionBuckets_477B60(s);
    s->set_xyz_lazy_420600(x, y, z);
    s->set_ang_lazy_420690(rot);
    if (CarPhysics* m = car->field_58_physics)
    {
        const Fix16 steer = m->field_78_pointing_ang_rad; // SetSprite_563560 zeroes the steering
        m->SetSprite_563560(s);
        m->field_78_pointing_ang_rad = steer;
    }
    gSpriteGrid_1_679208->AddToRegionBuckets_477B20(s);
}

void net::engine::SetCarPhysics(Car* car, const EntityState& e)
{
    CarPhysics* m = car->field_58_physics;
    if (!m || !e.f[kCarPhysics + kCp1X]) // no physics on the server
    {
        return;
    }
    for (size_t i = 0; i < sizeof(kPhysics) / sizeof(kPhysics[0]); i++)
    {
        SetMember(m, kPhysics[i], e.f[kCarPhysics + i]);
    }
}

void net::engine::DebugCarDiff(Car* car, const EntityState& e, uint32_t tick)
{
    static const bool on = getenv("GTA2_NET_CARDIFF") != NULL;
    const CarPhysics* m = car ? car->field_58_physics : NULL;
    if (!on || !m || !e.f[kCarPhysics + kCp1X])
    {
        return;
    }
    char line[2048];
    int n = 0;
    for (size_t i = 0; i < sizeof(kPhysics) / sizeof(kPhysics[0]) && n < (int)sizeof(line) - 80; i++)
    {
        const int32_t here = GetMember(m, kPhysics[i]), there = e.f[kCarPhysics + i];
        if (here != there)
        {
            n += snprintf(line + n, sizeof(line) - n, " %s %d/%d", kPhysics[i].name, here, there);
        }
    }
    if (n)
    {
        printf("net: tick %u, our car predicted (here/server):%s\n", tick, line);
    }
}

void net::engine::MovePed(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    Sprite* s = ped->field_168_game_object ? ped->field_168_game_object->field_80_sprite_ptr : NULL;
    if (!s || InCarTransition(ped) || ped->isDead_403B60())
    {
        // Getting in or out, or dying: the game keeps its char's place in the collision grid itself, and re-added, a link
        // stays behind when the char goes (a dead player's, moved where the server had it)
        return;
    }
    gSpriteGrid_1_679208->RemoveFromRegionBuckets_477B60(s);
    s->set_xyz_lazy_420600(x, y, z);
    ped->SetRotation_433C00(rot);
    gSpriteGrid_1_679208->AddToRegionBuckets_477B20(s);
    ped->field_1AC_cam.x = x;
    ped->field_1AC_cam.y = y;
    ped->field_1AC_cam.z = z;
}

bool net::engine::IsSpareDriver(const Ped* ped)
{
    return ped && gCarManager_6C_677930 && ped == gCarManager_6C_677930->GetSpareDriver_421960();
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
    EntityState w = Entity(kWorldId, kEntityWorld);
    w.f[kWorldCycle] = gpRng_67AB34->get_cur_rng_41CFE0();
    out.entities.push_back(w);
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
        e.f[kPlayerCar] = ped->field_16C_car ? ped->field_16C_car->field_6C_car_id + 1 : 0;
        e.f[kPlayerState] = (InCarTransition(ped) ? 1 : 0) | (ped->isDead_403B60() ? 2 : 0);
        out.entities.push_back(e);
    }
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        if (c->field_50_car_sprite)
        {
            EntityState e = Entity(kCarIdBase + (uint32_t)c->field_6C_car_id, kEntityCar);
            PutPosition(e, c->field_50_car_sprite, kCarX, kCarY, kCarZ, kCarRot);
            e.f[kCarModel] = c->field_84_car_info_idx;
            e.f[kCarDriver] = !c->field_54_driver ? kDriverNone : IsSpareDriver(c->field_54_driver) ? kDriverTraffic : kDriverPed;
            if (const CarPhysics* m = c->field_58_physics)
            {
                for (size_t i = 0; i < sizeof(kPhysics) / sizeof(kPhysics[0]); i++)
                {
                    e.f[kCarPhysics + i] = GetMember(m, kPhysics[i]);
                }
            }
            out.entities.push_back(e);
        }
    }
    // Drivers by their cars: a traffic car knows its driver (Car::field_54_driver), but the driver's own field_16C_car
    // needn't point back (measured: 1 of 12 did)
    std::unordered_map<const Ped*, Car*> driving;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        if (c->field_54_driver && c->field_50_car_sprite && !IsSpareDriver(c->field_54_driver))
        {
            driving[c->field_54_driver] = c;
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        // On foot or driving (a driver is where its car is; passengers: not yet); players are their own entities
        auto d = driving.find(p);
        Car* car = d != driving.end() ? d->second : NULL;
        Sprite* s = car ? car->field_50_car_sprite
                  : !p->field_16C_car && p->field_168_game_object ? p->field_168_game_object->field_80_sprite_ptr
                  : NULL;
        if (s && p->field_238_ped_type != ped_type::player_2 && (car || p->IsActive_433B40()) && !IsSpareDriver(p)) // (drivers aren't "active")
        {
            EntityState e = Entity(kPedIdBase + p->field_200_id, kEntityPed);
            PutPosition(e, s, kPedX, kPedY, kPedZ, kPedRot);
            e.f[kPedRemap] = (uint8_t)p->field_244_remap;
            e.f[kPedCar] = car ? car->field_6C_car_id + 1 : 0;
            e.f[kPedState] = InCarTransition(p) ? 1 : 0;
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

uint32_t net::engine::PlayerCarId(const EntityState& e)
{
    return e.type == kEntityPlayer && e.f[kPlayerCar] ? kCarIdBase + (uint32_t)(e.f[kPlayerCar] - 1) : 0;
}

int net::engine::PlayerDeaths(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerDeaths] : 0;
}

void net::engine::Apply(const Snapshot& snap)
{
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        if (snap.entities[k].type == kEntityWorld)
        {
            gpRng_67AB34->field_0_rng = snap.entities[k].f[kWorldCycle];
        }
    }
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
                if (ped->field_16C_car)
                {
                    // Out of the car first, the way leaving one ends: the game kills a player at the wheel only with its car
                    // (and clears the driver then); killed in its seat, the car kept the respawned ped as its driver, and a
                    // ped pulling that "driver" out gave it a second char (left in the collision grid)
                    ForceOutOfCar(ped, Fix16(e.f[kPlayerX], 0), Fix16(e.f[kPlayerY], 0), Fix16(e.f[kPlayerZ], 0), Ang16((s16)e.f[kPlayerRot], (u8)0));
                }
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
        // In the server's car, or on foot as the server has it: predicted by this game's own input, put right here
        // when both sides have settled (neither halfway into or out of a car, nobody dead)
        const bool settled = !(e.f[kPlayerState] & 3) && !InCarTransition(ped) && !ped->isDead_403B60();
        if (settled)
        {
            Car* serverCar = e.f[kPlayerCar] ? ReplicaCar(kCarIdBase + (uint32_t)(e.f[kPlayerCar] - 1)) : NULL;
            if (serverCar && ped->field_16C_car != serverCar)
            {
                const bool before = DebugGridOk("before forcing into a car", serverCar);
                ForceIntoCar(ped, serverCar);
                if (before)
                {
                    DebugGridOk("after forcing into a car", serverCar);
                }
            }
            else if (!e.f[kPlayerCar] && ped->field_16C_car)
            {
                Car* was = ped->field_16C_car;
                const bool before = DebugGridOk("before forcing out of a car", was);
                ForceOutOfCar(ped, x, y, z, rot);
                if (before)
                {
                    DebugGridOk("after forcing out of a car", was);
                }
            }
        }
        const bool before = DebugGridOk("before moving a player", ped->field_16C_car);
        Sprite* was = PedSprite(ped);
        // A jump of the local player (its first snapshot, a respawn): the camera goes there at once, not panning across
        // the map, as PutInGame and the game's respawn place it
        const bool jump = i == LocalPlayer() && was && (abs(was->field_14_xy.x.mValue - x.mValue) > (8 << 14) || abs(was->field_14_xy.y.mValue - y.mValue) > (8 << 14));
        if (ped->field_16C_car)
        {
            MoveCar(ped->field_16C_car, x, y, z, rot);
        }
        else
        {
            MovePed(ped, x, y, z, rot);
        }
        if (before)
        {
            DebugGridOk("after moving a player", ped->field_16C_car);
        }
        if (jump)
        {
            p->field_90_game_camera.UpdateFollowPedCamera_436540(ped);
            p->field_90_game_camera.CommitPendingTarget_41E410();
            p->field_90_game_camera.ResetCameraSmoothing_435DD0();
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

bool net::engine::PlayerInCar(int player)
{
    Player* p = GetPlayer(player);
    return p && p->field_8E_in_use && p->field_2C4_player_ped && p->field_2C4_player_ped->field_16C_car;
}

void net::engine::DebugOwnCarDiff(const Snapshot& snap)
{
    Player* p = GetPlayer(LocalPlayer());
    Car* car = p && p->field_2C4_player_ped ? p->field_2C4_player_ped->field_16C_car : NULL;
    if (!car)
    {
        return;
    }
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        if (snap.entities[k].type == kEntityCar && ReplicaCar(snap.entities[k].id) == car)
        {
            DebugCarDiff(car, snap.entities[k], snap.tick);
        }
    }
}

void net::engine::Poses(std::vector<Pose>& out)
{
    out.clear();
    for (int i = 0; i < PlayerCount(); i++)
    {
        Player* p = GetPlayer(i);
        Ped* ped = p && p->field_8E_in_use ? p->field_2C4_player_ped : NULL;
        Sprite* s = ped && !ped->field_16C_car && ped->field_168_game_object ? ped->field_168_game_object->field_80_sprite_ptr : NULL;
        if (s)
        {
            out.push_back(Pose{(uint32_t)(1 + i), s->field_14_xy.x.mValue, s->field_14_xy.y.mValue, s->field_1C_zpos.mValue, s->field_0_rotation.rValue});
        }
    }
    ReplicaPoses(out);
}

void net::engine::SetPose(const Pose& pose)
{
    if (pose.id >= kCarIdBase)
    {
        SetReplicaPose(pose);
        return;
    }
    Player* p = GetPlayer((int)pose.id - 1);
    Ped* ped = p && p->field_8E_in_use ? p->field_2C4_player_ped : NULL;
    if (ped && !ped->field_16C_car && !InCarTransition(ped) && !ped->isDead_403B60())
    {
        MovePed(ped, Fix16(pose.x, 0), Fix16(pose.y, 0), Fix16(pose.z, 0), Ang16((s16)pose.rot, (u8)0));
    }
}

namespace
{
bool gMarker;
Fix16 gMarkerX, gMarkerY, gMarkerZ;
} // namespace

void net::engine::SetServerMarker(int32_t x, int32_t y, int32_t z)
{
    gMarker = true;
    gMarkerX = Fix16(x, 0);
    gMarkerY = Fix16(y, 0);
    gMarkerZ = Fix16(z, 0);
}

void net::engine::DrawOverlay()
{
    Camera* cam = gViewCamera_676978;
    if (!gMarker || !cam)
    {
        return;
    }
    Fix16 sx, sy;
    cam->ProjectWorldToScreen_4B90E0(gMarkerX, gMarkerY, gMarkerZ, &sx, &sy);
    const float x = sx.ToFloat(), y = sy.ToFloat(), r = 4.0f;
    Vert v[4] = {};
    v[0].x = x - r, v[0].y = y - r;
    v[1].x = x + r, v[1].y = y - r;
    v[2].x = x + r, v[2].y = y + r;
    v[3].x = x - r, v[3].y = y + r;
    pgbh_DrawFlatRect(v, (s32)0xFFFF00FF); // magenta: where the server has us (a snapshot's tick, before the prediction)
}
