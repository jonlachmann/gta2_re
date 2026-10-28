// Port/net (docs/netcode.md, "Spawning"): a server's ambient traffic and peds for every player, not the first: the game's
// limits times the players (up to 3 players' worth), and each player's camera bounds out to where its client shows it.
#include "internal.hpp"
#include "dev/dev.hpp"
#include "Port/port_rand_owner.h"
#include "Port/port_hooks.hpp"
#include "Port/port_rand_state.h"
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

EXTERN_GLOBAL(u8, gNumDummyChars_6787E2);

namespace
{
int gSpawnLead[MAX_PLAYERS];

// The camera's bounds out to where its client already shows it: grown ahead as the camera moves (its last tick's step), so the
// traffic and peds spawn and go out of sight of what that player sees, not of where this game has it (Car.cpp,
// TrafficSpawner::MakeTrafficForCurrCamera_5832C0; Char_Pool.cpp; Car::ManageDespawning_442310)
void LeadCameras()
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player* p = gSpawnLead[i] ? net::engine::GetPlayer(i) : NULL;
        if (!p || !p->field_8E_in_use)
        {
            continue;
        }
        Camera* cams[2] = {&p->field_90_game_camera, &p->field_208_aux_game_camera};
        for (Camera* c : cams)
        {
            const s32 stepX = c->field_98_cur_pos.field_0_x.mValue - c->field_88_prev_pos.field_0_x.mValue;
            const s32 stepY = c->field_98_cur_pos.field_4_y.mValue - c->field_88_prev_pos.field_4_y.mValue;
            if (abs(stepX) > 1 << 14 || abs(stepY) > 1 << 14)
            {
                continue; // a jump (a respawn), not a move
            }
            const s32 dx = stepX * gSpawnLead[i], dy = stepY * gSpawnLead[i];
            // Its leading edges only: the others are where the client shows them too (moved, a spawn behind or beside the
            // player came in its sight)
            (dx < 0 ? c->field_78_view_bounds.field_0_left : c->field_78_view_bounds.field_4_right).mValue += dx;
            (dy < 0 ? c->field_78_view_bounds.field_8_top : c->field_78_view_bounds.field_C_bottom).mValue += dy;
        }
    }
}
} // namespace

// Up to 3 players' worth. A server's traffic cars have drivers of their own, and with 6 players' worth (96
// of them, 120 ambient peds and the police) the ped pool (200) ran out: a crash in the game's AI. More wants a server
// built with bigger pools (GTA2_POOL_SCALE)
void net::engine::SetSpawnPlayers(int players)
{
    gPortSpawnPlayers = players < 1 ? 1 : players > 3 ? 3 : players;
}

void net::engine::SetSpawnLead(int player, int ticks)
{
    if (player >= 0 && player < MAX_PLAYERS)
    {
        gSpawnLead[player] = ticks < 0 ? 0 : ticks > 30 ? 30 : ticks;
        gPortAfterPlayersHook = LeadCameras;
    }
}

std::string net::engine::SpawnStats()
{
    char line[160];
    const int dummies = gPedManager_6787BC ? gPedManager_6787BC->field_0_max_dummy_chars * gPortSpawnPlayers : 0;
    snprintf(line, sizeof(line), "traffic %d of %d, ambient peds %d of %d", gCarManager_6C_677930->field_28_recycled_cars + gCarManager_6C_677930->field_40_proto_recycled_cars,
             PortTrafficLimit(), (int)gNumDummyChars_6787E2, dummies < 80 ? dummies : 80);
    return line;
}

int net::engine::SpawnLead(int player)
{
    return player >= 0 && player < MAX_PLAYERS ? gSpawnLead[player] : 0;
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
