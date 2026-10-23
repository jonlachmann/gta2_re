// Port (docs/port.md): non-Windows builds have no network game; DirectPlay (NetPlay.cpp) and the lobby
// (NetUi.cpp) are left out. The game only uses them when a network game was started
// (bStartNetworkGame_7081F0), which can't happen here; this single-player NetPlay answers the few calls
// the rest of the game makes: no players, no messages.
#if !defined(_WIN32)
#include "Source/NetPlay.hpp"
#include "Source/NetUi.hpp"
#include <stdlib.h>
#include <string.h>

// Experiment (docs/netcode.md, "Evaluations"): GTA2_OFFLINE_MP=<players>:<gmp>:<sty>:<scr>[:<game type 1-3>], with -N
// on the command line, starts a local network game with that many players and no remote ones: only player 0 gets
// input, the others stand where the script put them. Not netcode, just whether the game boots that way.
namespace
{
const char* OfflineMp(int field)
{
    static char sFields[5][260];
    static int sCount = -1;
    if (sCount < 0)
    {
        sCount = 0;
        const char* env = getenv("GTA2_OFFLINE_MP");
        for (const char* p = env; p && *p && sCount < 5; sCount++)
        {
            const char* end = strchr(p, ':');
            const size_t n = end ? (size_t)(end - p) : strlen(p);
            memcpy(sFields[sCount], p, n < 259 ? n : 259);
            p = end ? end + 1 : p + n;
        }
    }
    return field < sCount ? sFields[field] : NULL;
}
} // namespace

DEFINE_GLOBAL(NetPlay, gNetPlay_7071E8, 0x7071E8);

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
    return OfflineMp(0) != NULL;
}
s32 NetPlay::InitializeConnection_51E5C0()
{
    return OfflineMp(0) != NULL;
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
    return OfflineMp(0) ? atoi(OfflineMp(0)) : 0;
}
bool NetPlay::WaitForPlayersSync_5213E0()
{
    return OfflineMp(0) != NULL;
}
s32 NetPlay::SendToPlayer_521630(Network_Buffer*, s32, char_type)
{
    return 0;
}
bool NetPlay::ReceiveGameMessage_521890(Network_Buffer*, s32*, u32*)
{
    return 0;
}
void NetPlay::SendToAll_521B20(Network_Buffer*)
{
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
// The lobby never shows: 0 is "cancelled", so Start_NetworkGame_5E5A30 doesn't start a network game.
s32 NetUi::ShowNetworkUiBlocking_519BD0(HINSTANCE)
{
    return OfflineMp(0) != NULL;
}
s32 __stdcall NetUi::cb_FillSessionList_519D30(NetUi*, Network_NameList*)
{
    return 0;
}
void NetUi::CopyGameSettings_51C7F0(NetworkGameSettings* pSettings)
{
    if (OfflineMp(0))
    {
        pSettings->field_20190_game_speed = 2; // 33 ms
        pSettings->field_20194_frag_limit = 10;
        pSettings->field_20198_game_type = OfflineMp(4) ? atoi(OfflineMp(4)) : 1; // frags
        pSettings->field_201A0_police_on = 0;
        pSettings->field_201A4_game_time_limit = 10;
    }
}
char_type* NetUi::GetMapName_51CA10()
{
    static char_type sEmpty[1];
    return OfflineMp(1) ? (char_type*)OfflineMp(1) : sEmpty;
}
char_type* NetUi::GetMapStyName_51CA50()
{
    static char_type sEmpty[1];
    return OfflineMp(2) ? (char_type*)OfflineMp(2) : sEmpty;
}
char_type* NetUi::GetMapScrName_51CA90()
{
    static char_type sEmpty[1];
    return OfflineMp(3) ? (char_type*)OfflineMp(3) : sEmpty;
}
#endif
