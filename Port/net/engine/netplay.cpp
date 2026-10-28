// Port/net (docs/netcode.md, "Code layout"): the game's network layer (NetPlay, NetUi) for the port, in place of
// DirectPlay (NetPlay.cpp, NetUi.cpp). Without a network game it answers like a single-player machine: no players,
// no messages. With one (StartNetworkGame, or the GTA2_OFFLINE_MP experiment) the game boots a network game with
// its settings, and NetPlay::SendToAll_521B20, which the game calls once per network frame after the tick, is the
// netcode's frame hook. Remote players are never "in use" here, so the game never waits for their messages: the
// netcode sets every player's input itself.
#include "engine.hpp"
#include "Source/NetPlay.hpp"
#include "Source/NetUi.hpp"
#include "Source/Game.hpp"
#include "Source/GameSession.hpp"
#include "Source/debug.hpp"
#include "Port/port_hooks.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DEFINE_GLOBAL(NetPlay, gNetPlay_7071E8, 0x7071E8);

namespace
{
net::engine::NetworkGame gNetGame;
bool gActive;
bool gChecked;
net::engine::FrameHook gHook;
bool gNextPending; // a map change: the game that ends goes straight into the next (gPortNextNetworkGame)
int gGeneration; // the games started since the boot's
void* gHookUser;
bool NextNetworkGame(); // gPortNextNetworkGame

// GTA2_OFFLINE_MP=<players>:<gmp>:<sty>:<scr>[:<game type 1-3>] (docs/netcode.md, "Evaluations", 4): a local
// network game without the netcode, only player 0 with input
void CheckEnvironment()
{
    if (gChecked)
    {
        return;
    }
    gChecked = true;
    const char* env = getenv("GTA2_OFFLINE_MP");
    if (gActive || !env)
    {
        return;
    }
    std::string fields[5];
    int n = 0;
    for (const char* p = env; *p && n < 5; n++)
    {
        const char* end = strchr(p, ':');
        fields[n] = end ? std::string(p, end) : std::string(p);
        p = end ? end + 1 : p + strlen(p);
    }
    net::engine::NetworkGame g;
    g.players = atoi(fields[0].c_str());
    g.gmp = fields[1];
    g.sty = fields[2];
    g.scr = fields[3];
    g.gameType = n > 4 ? atoi(fields[4].c_str()) : 1;
    net::engine::StartNetworkGame(g);
}

bool Active()
{
    CheckEnvironment();
    return gActive;
}
} // namespace

int net::engine::MaxWantedStars()
{
    return gNetGame.police;
}

void net::engine::StartNetworkGame(const NetworkGame& game)
{
    gNetGame = game;
    gActive = true;
    gChecked = true;
    gPortNextNetworkGame = NextNetworkGame;
    gNetPlay_7071E8.field_5D4_player_idx = game.localPlayer;
    gNetPlay_7071E8.field_758_n2.field_4_players_count = 2; // > 1: the game calls SendToAll every frame
}

namespace
{
bool gLeftForMenu;
} // namespace

void net::engine::LeaveForMenu()
{
    gLeftForMenu = true;
    if (gGame_0x40_67E008)
    {
        gGame_0x40_67E008->ExitGameNoBonus_4B8C00(1, GameExitType::PlayerQuit_2);
    }
}

bool net::engine::LeftForMenu()
{
    return gLeftForMenu;
}

// What a client's network game set that a single-player game would keep: the network game itself (the NetPlay
// answers), the client mode's switches (no traffic, peds, scripts, police: set every frame in ClientModeFrame), the
// replicas, the status over the frame
void net::engine::StopNetworkGame()
{
    gLeftForMenu = false;
    gActive = false;
    gNetPlay_7071E8.field_5D4_player_idx = 0;
    gNetPlay_7071E8.field_758_n2.field_4_players_count = 0;
    // ponytail: off, not as before: a network game cleared them at its start (ClearDebugFlags_4DB170)
    bSkip_recycling_67D575 = false;
    bSkip_dummies_67D4EF = false;
    bSkip_mission_67D4E5 = false;
    bSkip_police_67D4F9 = false;
    bSkip_ambulance_67D6C9 = false;
    gPortKeepWantedPoints = false;
    ClearRandOwners();
    ResetForNewGame();
    gGeneration++;
    SetNetStatus("", "");
}

namespace
{
// winmain.cpp, a network game over: the next at once when the netcode changed the map
bool NextNetworkGame()
{
    if (!gNextPending)
    {
        return false;
    }
    gNextPending = false;
    // The game takes the map's files from its session, which Start_NetworkGame_5E5A30 set once for the first game
    char path[260];
    snprintf(path, sizeof(path), "data\\%s", gNetGame.gmp.c_str());
    gGameSession_67E8E0.SetMapName_4C5870(path);
    snprintf(path, sizeof(path), "data\\%s", gNetGame.sty.c_str());
    gGameSession_67E8E0.SetStyleName_4C5890(path);
    snprintf(path, sizeof(path), "data\\%s", gNetGame.scr.c_str());
    gGameSession_67E8E0.SetScriptName_4C58B0(path);
    net::engine::ResetForNewGame();
    gGeneration++;
    return true;
}
} // namespace


void net::engine::SetNextMap(const std::string& gmp, const std::string& sty, const std::string& scr)
{
    gNetGame.gmp = gmp;
    gNetGame.sty = sty;
    gNetGame.scr = scr;
    gNextPending = true;
}

bool net::engine::MapChangePending()
{
    return gNextPending;
}

int net::engine::GameGeneration()
{
    return gGeneration;
}

void net::engine::SetFrameHook(FrameHook hook, void* user)
{
    gHook = hook;
    gHookUser = user;
}

NetPlay::NetPlay()
{
    field_4_bModem = 0;
    field_5_modem_num = 0;
    field_5CC_bIsHost = 0;
    field_5D4_player_idx = 0;
    field_8F0_receive_disabled = 1;
    field_8F4_time_diff = 0;
    field_CB8_count = 0;
    memset(&field_758_n2, 0, sizeof(field_758_n2));
}

NetPlay::~NetPlay()
{
}

s32 NetPlay::SetProtoAndConnection_51DAE0(GUID*, Network_Buffer*)
{
    return Active();
}
s32 NetPlay::InitializeConnection_51E5C0()
{
    return Active();
}
void NetPlay::Disconnect_520D10()
{
}
s32 NetPlay::DeletePlayerFromGroup_521000(u32)
{
    return 0;
}
void NetPlay::GetPlayerName_521100(wchar_t* Destination, u32)
{
    Destination[0] = 0;
}
void NetPlay::SetExitGameCallBack_521330(s32, Game*)
{
}
s32 NetPlay::CountActivePlayers_521350()
{
    return Active() ? gNetGame.players : 0;
}
bool NetPlay::WaitForPlayersSync_5213E0()
{
    return Active();
}
s32 NetPlay::SendToPlayer_521630(Network_Buffer*, s32, char_type)
{
    return 0;
}
bool NetPlay::ReceiveGameMessage_521890(Network_Buffer*, s32*, u32*)
{
    return 0;
}
// The game sends this machine's input for the next tick: the netcode's frame hook
void NetPlay::SendToAll_521B20(Network_Buffer*)
{
    if (gHook)
    {
        gHook(gHookUser);
    }
}
s32 NetPlay::SendPlayerIsQuitting_521D20()
{
    return 0;
}
s32 NetPlay::SendGiveUp_521DB0(s32)
{
    return 0;
}

NetUi::NetUi()
{
}
NetUi::~NetUi()
{
}
// The lobby: "cancelled" (0) without a network game, so Start_NetworkGame_5E5A30 starts none
s32 NetUi::ShowNetworkUiBlocking_519BD0(HINSTANCE)
{
    return Active();
}
s32 __stdcall NetUi::cb_FillSessionList_519D30(NetUi*, Network_NameList*)
{
    return 0;
}
void NetUi::CopyGameSettings_51C7F0(NetworkGameSettings* pSettings)
{
    if (Active())
    {
        pSettings->field_20190_game_speed = 2; // 33 ms; the netcode sets its own tick length after the boot
        pSettings->field_20194_frag_limit = 0x7FFFFFFF; // the limits are the netcode's (rounds.cpp): none the game would end on
        pSettings->field_20198_game_type = gNetGame.gameType;
        pSettings->field_201A0_police_on = gNetGame.police != 0;
        pSettings->field_201A4_game_time_limit = 0; // (0: no timer)
    }
}
char_type* NetUi::GetMapName_51CA10()
{
    return (char_type*)gNetGame.gmp.c_str();
}
char_type* NetUi::GetMapStyName_51CA50()
{
    return (char_type*)gNetGame.sty.c_str();
}
char_type* NetUi::GetMapScrName_51CA90()
{
    return (char_type*)gNetGame.scr.c_str();
}
