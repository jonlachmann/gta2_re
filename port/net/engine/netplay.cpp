// port/net (port/docs/netcode.md, "Code layout"): the game's network layer (NetPlay, NetUi) for the port, in place of
// DirectPlay (NetPlay.cpp, NetUi.cpp). Without a network game it answers like a single-player machine: no players,
// no messages. With one (StartNetworkGame, or the GTA2_OFFLINE_MP experiment) the game boots a network game with
// its settings, and NetPlay::SendToAll, which the game calls once per network frame after the tick, is the
// netcode's frame hook. Remote players are never "in use" here, so the game never waits for their messages: the
// netcode sets every player's input itself.
#include "internal.hpp"
#include "Source/NetPlay.hpp"
#include "Source/NetUi.hpp"
#include "Source/Game.hpp"
#include "Source/GameSession.hpp"
#include "Source/debug.hpp"
#include "port/src/net_hooks.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DEFINE_GLOBAL(NetPlay, gNetPlay, 0x7071E8);

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

// gPortWorldStill: the results between rounds, on the server and the clients alike (the round's state is in the world entity)
bool WorldStill()
{
    return net::WorldStandsStill(net::engine::CurrentRound().phase);
}

// GTA2_OFFLINE_MP=<players>:<gmp>:<sty>:<scr>[:<game type 1-3>] (port/docs/netcode.md, "Measurements behind the design"): a local
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
    gPortWorldStill = WorldStill;
    gNetPlay.mPlayerIdx = game.localPlayer;
    gNetPlay.mN2.mPlayersCount = 2; // > 1: the game calls SendToAll every frame
}

namespace
{
bool gLeftForMenu;
} // namespace

void net::engine::LeaveForMenu()
{
    gLeftForMenu = true;
    if (gGame_0x40)
    {
        gGame_0x40->ExitGameNoBonus(1, GameExitType::PlayerQuit_2);
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
    gPortWorldStill = NULL;
    gNetPlay.mPlayerIdx = 0;
    gNetPlay.mN2.mPlayersCount = 0;
    // Off, not as before: a network game cleared them at its start (ClearDebugFlags)
    bSkip_recycling = false;
    bSkip_dummies = false;
    bSkip_mission = false;
    bSkip_police = false;
    bSkip_ambulance = false;
    gPortKeepWantedPoints = false;
    gPortServerKillNotices = false;
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
    // The game takes the map's files from its session, which Start_NetworkGame set once for the first game
    char path[260];
    snprintf(path, sizeof(path), "data\\%s", gNetGame.gmp.c_str());
    gGameSession.SetMapName(path);
    snprintf(path, sizeof(path), "data\\%s", gNetGame.sty.c_str());
    gGameSession.SetStyleName(path);
    snprintf(path, sizeof(path), "data\\%s", gNetGame.scr.c_str());
    gGameSession.SetScriptName(path);
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
    mbModem = 0;
    mModemNum = 0;
    mbIsHost = 0;
    mPlayerIdx = 0;
    mReceiveDisabled = 1;
    mTimeDiff = 0;
    mCount = 0;
    memset(&mN2, 0, sizeof(mN2));
}

NetPlay::~NetPlay()
{
}

s32 NetPlay::SetProtoAndConnection(GUID*, Network_Buffer*)
{
    return Active();
}
s32 NetPlay::InitializeConnection()
{
    return Active();
}
void NetPlay::Disconnect()
{
}
s32 NetPlay::DeletePlayerFromGroup(u32)
{
    return 0;
}
void NetPlay::GetPlayerName(wchar_t* Destination, u32)
{
    Destination[0] = 0;
}
void NetPlay::SetExitGameCallBack(s32, Game*)
{
}
s32 NetPlay::CountActivePlayers()
{
    return Active() ? gNetGame.players : 0;
}
bool NetPlay::WaitForPlayersSync()
{
    return Active();
}
s32 NetPlay::SendToPlayer(Network_Buffer*, s32, char_type)
{
    return 0;
}
bool NetPlay::ReceiveGameMessage(Network_Buffer*, s32*, u32*)
{
    return 0;
}
// The game sends this machine's input for the next tick: the netcode's frame hook
void NetPlay::SendToAll(Network_Buffer*)
{
    if (gHook)
    {
        gHook(gHookUser);
    }
}
s32 NetPlay::SendPlayerIsQuitting()
{
    return 0;
}
s32 NetPlay::SendGiveUp(s32)
{
    return 0;
}

NetUi::NetUi()
{
}
NetUi::~NetUi()
{
}
// The lobby: "cancelled" (0) without a network game, so Start_NetworkGame starts none
s32 NetUi::ShowNetworkUiBlocking(HINSTANCE)
{
    return Active();
}
s32 __stdcall NetUi::cb_FillSessionList(NetUi*, Network_NameList*)
{
    return 0;
}
void NetUi::CopyGameSettings(NetworkGameSettings* pSettings)
{
    if (Active())
    {
        pSettings->mGameSpeed = 2; // 33 ms; the netcode sets its own tick length after the boot
        pSettings->mFragLimit = 0x7FFFFFFF; // the limits are the netcode's (rounds.cpp): none the game would end on
        pSettings->mGameType = gNetGame.gameType;
        pSettings->mPoliceOn = gNetGame.police != 0;
        pSettings->mGameTimeLimit = 0; // (0: no timer)
    }
}
char_type* NetUi::GetMapName()
{
    return (char_type*)gNetGame.gmp.c_str();
}
char_type* NetUi::GetMapStyName()
{
    return (char_type*)gNetGame.sty.c_str();
}
char_type* NetUi::GetMapScrName()
{
    return (char_type*)gNetGame.scr.c_str();
}
