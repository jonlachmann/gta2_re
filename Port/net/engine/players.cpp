// Port/net (docs/netcode.md, "Health, deaths, weapons, scores", "Players in cars"): the players as snapshot entities,
// and the server's put into a client's game: health, deaths, weapons, power-ups, its car, its place.
#include "internal.hpp"
#include "Port/port_hooks.hpp"
#include "Port/port_rand_state.h"
#include "Source/Camera.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/GameSession.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/Weapon.hpp"
#include "Source/char.hpp"
#include "Source/network_data.hpp"
#include "Source/rng.hpp"
#include "Source/sprite.hpp"
#include <math.h>
#include <stdlib.h>
#include <string>

EXTERN_GLOBAL(Network_PlayerInputs, gPrevNetInputs_6F5B28);

namespace
{
using namespace net::engine;
using net::EntityState;
using net::Snapshot;

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

std::string NarrowName(const Player* p)
{
    std::string n;
    for (int k = 0; k < 16 && p->field_83C_player_name[k]; k++)
    {
        n += (char)p->field_83C_player_name[k];
    }
    return n;
}

// 4 characters a field
void PackName(const Player* p, net::EntityState& e)
{
    for (int k = 0; k < 16; k++)
    {
        e.f[kPlayerName + k / 4] |= (int32_t)((uint32_t)(p->field_83C_player_name[k] & 0xFF) << (8 * (k % 4)));
    }
}

std::string UnpackName(const net::EntityState& e)
{
    char name[17] = {};
    for (int k = 0; k < 16; k++)
    {
        name[k] = (char)(((uint32_t)e.f[kPlayerName + k / 4] >> (8 * (k % 4))) & 0xFF);
    }
    return name;
}

void CountDeath(int i, Player* p, Ped* ped)
{
    if (i >= MAX_PLAYERS)
    {
        return;
    }
    const bool dead = p->field_28_wasted_or_busted != 0;
    if (dead && !gWasDead[i])
    {
        gDeaths[i]++;
        gKiller[i] = (uint8_t)(PlayerIndexOfPed(ped->field_204_killer_id) + 1);
    }
    gWasDead[i] = dead;
}

// Weapons and ammo are the server's
void ApplyWeapons(Player* p, const net::EntityState& e)
{
    if (e.f[kPlayerWeapon] >= 1 && e.f[kPlayerWeapon] <= kPlayerWeapons)
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
}

// Power-ups too (one collected here in a prediction the server didn't have ends here), with what they switch on the
// ped (Player::CollectPowerUp_564D60, tick_down_powerups_565070); invulnerability is the client's anyway
void ApplyPowerUps(Player* p, Ped* ped, const net::EntityState& e)
{
    for (int k = 0; k < kPowerUps; k++)
    {
        const u16 now = (u16)e.f[kPlayerPowerUp + k];
        if (!p->field_6F4_power_up_timers[k] != !now)
        {
            if (k == power_up_indices::Electrofingers_9)
            {
                now ? ped->SetElectrofingers_4A5060() : ped->ClearElectrofingers_482080();
            }
            else if (k == power_up_indices::Invisibility_11)
            {
                now ? ped->SetInvisible() : ped->SetVisible();
            }
        }
        p->field_6F4_power_up_timers[k] = now;
    }
}

// A death the server had and this game hasn't carried out: the game's own death, credited to the killer, where this
// game has the ped (the server has already respawned it: the next snapshot brings it there). True: carried out now
bool CarryOutDeath(int i, Player* p, Ped* ped, const net::EntityState& e)
{
    if (i >= MAX_PLAYERS || (uint8_t)e.f[kPlayerDeaths] == gNetDeaths.mDone[i])
    {
        return false;
    }
    gNetDeaths.mDone[i] = (uint8_t)e.f[kPlayerDeaths];
    if (ped->isDead_403B60() || p->field_28_wasted_or_busted)
    {
        return false;
    }
    if (ped->field_16C_car)
    {
        // Out of the car first, the way leaving one ends: the game kills a player at the wheel only with its car (and
        // clears the driver then); killed in its seat, the car kept the respawned ped as its driver, and a ped pulling
        // that "driver" out gave it a second char (left in the collision grid)
        ForceOutOfCar(ped, Raw(e.f[kPlayerX]), Raw(e.f[kPlayerY]), Raw(e.f[kPlayerZ]), Angle(e.f[kPlayerRot]));
    }
    KillCredited(ped, e.f[kPlayerKiller] - 1);
    return true;
}

// The car it is going to get into, as the server has it, while it is still on its way (not at the door yet): in a jam
// the nearest car in front (CarManager::GetNearestFrontVehicle_445210) can be another one here
void RetargetEntering(Ped* ped, Car* serverTarget, const net::EntityState& e)
{
    if (serverTarget && serverTarget->field_50_car_sprite && !ped->field_16C_car && !ped->isDead_403B60() && !(e.f[kPlayerState] & kPlayerDead) &&
        ped->get_objective_403A80() == objectives_enum::enter_car_as_driver_35 && ped->field_150_target_objective_car != serverTarget &&
        ped->field_278_ped_state_1 != ped_state_1::entering_car_3)
    {
        StartEntering(ped, serverTarget);
    }
}

// In the server's car, or on foot as the server has it: predicted by this game's own input, put right here when both
// sides have settled (neither halfway into or out of a car, nobody dead) and not counting down to its respawn here
// (wasted): the ped waits at the restart zone in a plain state, and put into the server's car (respawned there
// already) its char went, which the respawn reads (Ped::ResetForPlayerRespawn_45C410)
void SettleCar(Player* p, Ped* ped, const net::EntityState& e)
{
    const bool settled = !(e.f[kPlayerState] & (kPlayerTransition | kPlayerDead)) && !InCarTransition(ped) && !ped->isDead_403B60() &&
                         !p->field_28_wasted_or_busted;
    if (!settled)
    {
        return;
    }
    Car* serverCar = RefCar(e.f[kPlayerCar]);
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
        ForceOutOfCar(ped, Raw(e.f[kPlayerX]), Raw(e.f[kPlayerY]), Raw(e.f[kPlayerZ]), Angle(e.f[kPlayerRot]));
        if (before)
        {
            DebugGridOk("after forcing out of a car", was);
        }
    }
}

// Its place: its car's, or its own on foot. True: a jump of the local player (its first snapshot, a respawn), which
// the camera follows at once instead of panning across the map, as PutInGame and the game's respawn place it
bool MovePlayer(int i, Ped* ped, const net::EntityState& e)
{
    const Fix16 x = Raw(e.f[kPlayerX]), y = Raw(e.f[kPlayerY]), z = Raw(e.f[kPlayerZ]);
    const Ang16 rot = Angle(e.f[kPlayerRot]);
    const bool before = DebugGridOk("before moving a player", ped->field_16C_car);
    Sprite* was = PedSprite(ped);
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
    return jump;
}

// The server's player on its way into or out of a car and this game's not: another player's key press came before
// the ticks this game resimulates, so its ped stood by the car until the server had it inside. Started here from the
// server's position, as the key starts it (Player::HandleControls_5668D0)
void StartTransition(Player* p, Ped* ped, Car* serverTarget, const net::EntityState& e)
{
    if ((e.f[kPlayerState] & (kPlayerTransition | kPlayerDead)) != kPlayerTransition || InCarTransition(ped) || ped->isDead_403B60() ||
        p->field_28_wasted_or_busted)
    {
        return;
    }
    Car* car = ped->field_16C_car;
    if (!car && serverTarget && serverTarget->field_50_car_sprite && !(e.f[kPlayerState] & kPlayerLeaving) && ped->field_168_game_object)
    {
        StartEntering(ped, serverTarget);
    }
    else if (car && (e.f[kPlayerState] & kPlayerLeaving) && RefCar(e.f[kPlayerCar]) == car && car->CanExitCar_43AF10())
    {
        StartLeaving(ped);
    }
}
} // namespace

void net::engine::CapturePlayer(int i, Snapshot& out)
{
    Player* p = GetPlayer(i);
    Ped* ped = InGamePed(i); // slots out of the game aren't sent
    Sprite* s = ped ? PedSprite(ped) : NULL;
    if (!s)
    {
        return;
    }
    EntityState e = Entity(1 + i, kEntityPlayer);
    PutPosition(e, s);
    e.f[kPlayerInCar] = ped->field_16C_car != NULL;
    // The controls the tick ran with (winmain.cpp's Net_Set_Local_Player_Inputs_4DAD50 takes them from here; the
    // player's own copy, Player::field_4_inputs, is zeroed once used): what clients predict this player with
    e.f[kPlayerButtons] = (int32_t)(gPrevNetInputs_6F5B28.field_0_inputs[i].field_0_Inputs & 0x3FFFFF);
    e.f[kPlayerHealth] = ped->field_216_health;
    CountDeath(i, p, ped);
    e.f[kPlayerDeaths] = i < MAX_PLAYERS ? gDeaths[i] : 0;
    e.f[kPlayerKiller] = i < MAX_PLAYERS ? gKiller[i] : 0;
    e.f[kPlayerWeapon] = p->field_788_curr_weapon_idx + 1;
    e.f[kPlayerFrags] = gGameSession_67E8E0.field_490_frags_list[i];
    e.f[kPlayerPoints] = gGameSession_67E8E0.field_49C_points_list[i];
    for (int w = 0; w < kPlayerWeapons; w++)
    {
        e.f[kPlayerAmmo0 + w] = p->field_718_weapons[w] ? p->field_718_weapons[w]->field_0_ammo : 0;
    }
    e.f[kPlayerCar] = CarRef(ped->field_16C_car);
    e.f[kPlayerState] = (InCarTransition(ped) ? kPlayerTransition : 0) | (ped->isDead_403B60() ? kPlayerDead : 0) | (LeavingCar(ped) ? kPlayerLeaving : 0);
    e.f[kPlayerTarget] = ped->get_objective_403A80() == objectives_enum::enter_car_as_driver_35 ? CarRef(ped->field_150_target_objective_car) : 0;
    PackName(p, e);
    for (int k = 0; k < kPowerUps; k++)
    {
        e.f[kPlayerPowerUp + k] = p->field_6F4_power_up_timers[k];
    }
    out.entities.push_back(e);
}

void net::engine::ApplyWorld(const EntityState& w)
{
    gpRng_67AB34->field_0_rng = w.f[kWorldCycle];
    gPortZoneRoundRobin = (short)w.f[kWorldZoneTurn];
    CurrentRound() = Round{w.f[kWorldRound], w.f[kWorldWinner], w.f[kWorldSecondsLeft]};
    // The tag game's "it" is the server's, made the game's way (its weapons off, the arrow, "you're it")
    Player* it = GetPlayer(w.f[kWorldIt] - 1);
    if (it && it != gTagGame_6F8450.field_0_fugitive && gTagGame_6F8450.IsTagGame_434B20())
    {
        gTagGame_6F8450.SetNewFugitive_516590(it);
    }
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        gTagGame_6F8450.field_4_it_time_secs[i] = w.f[kWorldItTime + i];
    }
    PortRandSetState((unsigned int)w.f[kWorldRand]);
}

void net::engine::ApplySlots(const Snapshot& snap)
{
    // The slots the server has in the game, and only those (a join or a leave on the server: the same here)
    bool sent[MAX_PLAYERS] = {};
    for (const EntityState& e : snap.entities)
    {
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
}

void net::engine::ApplyPlayer(const EntityState& e)
{
    const int i = (int)e.id - 1;
    Player* p = GetPlayer(i);
    Ped* ped = p ? p->field_2C4_player_ped : NULL;
    if (!ped)
    {
        return;
    }
    const std::string name = UnpackName(e);
    if (!name.empty() && NarrowName(p) != name)
    {
        SetPlayerName(i, name.c_str());
    }
    ApplyWeapons(p, e);
    ApplyPowerUps(p, ped, e);
    if (CarryOutDeath(i, p, ped, e))
    {
        return;
    }
    if (!ped->isDead_403B60())
    {
        // At least 1: a death here comes from the server's death count (above), whole; the health the server's dying
        // ped has (0) started a death of this game's own, undone by the next restore (a flicker of dying and living)
        ped->set_health_4039A0((s16)(e.f[kPlayerHealth] > 0 ? e.f[kPlayerHealth] : 1));
    }
    Car* serverTarget = RefCar(e.f[kPlayerTarget]);
    RetargetEntering(ped, serverTarget, e);
    SettleCar(p, ped, e);
    const bool jump = MovePlayer(i, ped, e);
    StartTransition(p, ped, serverTarget, e);
    if (jump)
    {
        SnapCamera(p);
    }
}

void net::engine::ApplyScores(const Snapshot& snap)
{
    for (const EntityState& e : snap.entities)
    {
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

void net::engine::KillCredited(Ped* ped, int killerSlot)
{
    Player* killer = GetPlayer(killerSlot);
    ped->field_204_killer_id = killer && killer->field_2C4_player_ped ? killer->field_2C4_player_ped->field_200_id : 0;
    ped->field_208_invulnerability = 0;
    ped->Kill_46F9D0();
}

void net::engine::SnapCamera(Player* p)
{
    p->field_90_game_camera.UpdateFollowPedCamera_436540(p->field_2C4_player_ped);
    p->field_90_game_camera.CommitPendingTarget_41E410();
    p->field_90_game_camera.ResetCameraSmoothing_435DD0();
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

int net::engine::PlayerPoints(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerPoints] : 0;
}

int net::engine::PlayerFrags(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerFrags] : 0;
}

uint32_t net::engine::PlayerCarId(const EntityState& e)
{
    return e.type == kEntityPlayer ? CarRefNetId(e.f[kPlayerCar]) : 0;
}

int net::engine::PlayerWeapon(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerWeapon] - 1 : -1;
}

int net::engine::PlayerAmmo(const EntityState& e, int weapon)
{
    return e.type == kEntityPlayer && weapon >= 0 && weapon < kPlayerWeapons ? e.f[kPlayerAmmo0 + weapon] : 0;
}

int net::engine::PlayerDeaths(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerDeaths] : 0;
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

bool net::engine::PlayerPosition(int player, float* pX, float* pY)
{
    float z;
    int rot;
    return PlayerPose(player, pX, pY, &z, &rot);
}

bool net::engine::PlayerWasted(int player)
{
    Player* p = GetPlayer(player);
    return p && p->field_28_wasted_or_busted;
}

bool net::engine::PlayerInCar(int player)
{
    Ped* ped = InGamePed(player);
    return ped && ped->field_16C_car;
}

float net::engine::PlayerViewRadius(int player)
{
    // How far its camera shows, centre to corner, at ground level (the inverse of Camera::ProjectWorldToScreen_4B90E0;
    // the camera rises as the player drives faster)
    Player* p = GetPlayer(player);
    if (!p)
    {
        return 0;
    }
    const Camera& c = p->field_90_game_camera;
    const float scale = c.field_60_screen_scale.x.ToFloat();
    if (scale <= 0)
    {
        return 0;
    }
    const float depth = 8.0f + c.field_98_cur_pos.field_8_z.ToFloat(); // (8 - z) + camera z, at z 0
    const float hw = c.field_70_screen_px_center_x * depth / scale, hh = c.field_74_screen_px_center_y * depth / scale;
    return sqrtf(hw * hw + hh * hh);
}

std::string net::engine::PlayerName(int slot)
{
    Player* p = GetPlayer(slot);
    return p ? NarrowName(p) : std::string();
}

void net::engine::ResetDeaths(int slot)
{
    if (slot >= 0 && slot < MAX_PLAYERS)
    {
        gDeaths[slot] = 0;
        gKiller[slot] = 0;
    }
}

void net::engine::SetPlayerName(int slot, const char* name)
{
    Player* p = GetPlayer(slot);
    if (!p)
    {
        return;
    }
    int k = 0;
    for (; k < 15 && name[k]; k++)
    {
        p->field_83C_player_name[k] = (wchar_t)(unsigned char)name[k];
    }
    p->field_83C_player_name[k] = 0;
    gGameSession_67E8E0.SetPlayerName_4C5C30((u16)slot, p->field_83C_player_name); // as Player.cpp sets it up
}

void net::engine::DebugKill(int victim, int killer)
{
    Player* p = GetPlayer(victim);
    Ped* ped = p ? p->field_2C4_player_ped : NULL;
    if (ped && !ped->isDead_403B60())
    {
        KillCredited(ped, killer);
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

void net::engine::DebugArm(int player, int weapon, int ammo)
{
    Player* p = GetPlayer(player);
    if (p && p->field_2C4_player_ped && weapon >= 0 && weapon < kPlayerWeapons && p->field_718_weapons[weapon] &&
        p->field_718_weapons[weapon]->field_0_ammo == 0)
    {
        p->AddWeaponWithAmmo_564960(weapon, (u8)ammo);
    }
}
