// Port/net (docs/netcode.md, "Joining"): player slots coming into and going out of a running network game. A slot
// without a client is out of the game the way the game handles a player who left (Player::Disconnect_568730:
// its ped walks on as a pedestrian). A slot that gets a client gets a new player ped at the next restart zone,
// as the map script's PLAYER_PED makes one (ScriptThread::SCRCMD_PLAYER_PED_503A20) and Player::InitPlayerPed_565490
// sets it up, but with the slot's own weapons kept: InitPlayerPed_565490 would allocate a new set each time.
#include "internal.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/GameSession.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/Weapon.hpp"
#include "Source/sprite.hpp"
#include "Source/Car.hpp"
#include "Source/map_0x370.hpp"
#include "Source/ped_graphic_type.hpp"

namespace
{
u8 gRemap[MAX_PLAYERS]; // a slot's look, from the ped the map script gave it

Player* Slot(int slot)
{
    Game* g = gGame_0x40_67E008;
    return g && slot >= 0 && slot < (int)g->field_23_num_players ? g->field_4_players[slot] : NULL;
}
} // namespace

bool net::engine::SlotInGame(int slot)
{
    Player* p = Slot(slot);
    return p && p->field_8E_in_use && p->field_2C4_player_ped;
}
// (A slot out of the game keeps its last ped pointer, a pedestrian now, as after Player::Disconnect_568730; only
// slots in the game count anywhere in the netcode)

void net::engine::TakeOutOfGame(int slot)
{
    Player* p = Slot(slot);
    if (!p || !p->field_8E_in_use)
    {
        return;
    }
    if (p->field_2C4_player_ped && slot < MAX_PLAYERS && !gRemap[slot])
    {
        gRemap[slot] = (u8)p->field_2C4_player_ped->field_244_remap;
    }
    if (Ped* ped = p->field_2C4_player_ped)
    {
        // Out of its car first: the ped walks on as a pedestrian, a local slot's camera follows it, and a car it sat in
        // can be despawned under it (the sound read the freed car through the camera)
        if (ped->field_16C_car && !InCarTransition(ped))
        {
            Sprite* s = ped->field_16C_car->field_50_car_sprite;
            if (s)
            {
                ForceOutOfCar(ped, s->field_14_xy.x + Fix16(1), s->field_14_xy.y, s->field_1C_zpos, s->field_0_rotation);
            }
        }
    }
    p->Disconnect_568730(); // keeps its ped pointer, as the game does: the HUD and camera of a local slot use it
}

void net::engine::PutInGame(int slot, bool freshScore)
{
    Player* p = Slot(slot);
    gmp_map_zone* zone = gMap_0x370_6F6268->GetNextZoneOfTypeRoundRobin_4DF0F0(0x10); // a Restart zone, round robin
    if (!p || !zone)
    {
        return;
    }
    if (freshScore && slot < MAX_PLAYERS)
    {
        gGameSession_67E8E0.field_490_frags_list[slot] = 0;
        gGameSession_67E8E0.field_49C_points_list[slot] = 0;
    }
    if (p->field_8E_in_use && p->field_2C4_player_ped)
    {
        p->field_2C4_player_ped->RespawnPed_45C350(zone); // still in: a fresh start where the others respawn
        return;
    }
    const Fix16 x = Fix16(zone->field_1_x + zone->field_3_w / 2), y = Fix16(zone->field_2_y + zone->field_4_h / 2);
    const Fix16 z = gMap_0x370_6F6268->FindGroundZForCoord_4E5B60(x, y);
    const u8 remap = slot < MAX_PLAYERS && gRemap[slot] ? gRemap[slot] : 0;
    Ped* ped = gPedManager_6787BC->SpawnPedAt(x, y, z, remap, Ang16((s16)0, (u8)0));
    if (!ped)
    {
        return;
    }
    ped->SetPedType_403920(ped_type::player_2);
    ped->set_health_4039A0(100);
    // Player::InitPlayerPed_565490, without new weapons
    p->field_28_wasted_or_busted = 0;
    p->field_640_busted = 0;
    p->field_29_aux_ped_dying = 0;
    p->field_2C_death_countdown = 0;
    p->field_2C4_player_ped = ped;
    p->field_2C8_aux_ped = NULL;
    p->field_680_traffic_spawn_counter = 0;
    p->field_682_traffic_spawn_threshold = 1000;
    ped->SetPlayer_45B560(p, 0);
    p->field_68_camera_mode = 0;
    for (int w = 0; w < 28; w++)
    {
        if (Weapon* weapon = p->field_718_weapons[w])
        {
            weapon->set_pPed_4CCA10(ped);
            weapon->field_0_ammo = 0;
        }
    }
    p->field_788_curr_weapon_idx = -1;
    p->field_14_saved_ped_weapon_idx = -1;
    ped->field_26C_graphic_type = ped_graphic_type::character_1;
    p->set_in_use_4A5170(1);
    ped->RespawnPed_45C350(zone); // a free spot in the zone, health, a moment of invulnerability
    p->field_90_game_camera.UpdateFollowPedCamera_436540(ped);
    p->field_90_game_camera.CommitPendingTarget_41E410();
    p->field_90_game_camera.ResetCameraSmoothing_435DD0();
}
