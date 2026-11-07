// Port (docs/port.md): the port has no network game of the original's; DirectPlay (NetPlay.cpp) and the lobby
// (NetUi.cpp) are left out. The game only uses them when a network game was started
// (bStartNetworkGame_7081F0), which can't happen here; this single-player NetPlay answers the few calls
// the rest of the game makes: no players, no messages.
#include "Source/NetPlay.hpp"
#include "Source/NetUi.hpp"
#include <string.h>

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
    return 0;
}
s32 NetPlay::InitializeConnection_51E5C0()
{
    return 0;
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
    return 0;
}
bool NetPlay::WaitForPlayersSync_5213E0()
{
    return false;
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
    return 0;
}
s32 __stdcall NetUi::cb_FillSessionList_519D30(NetUi*, Network_NameList*)
{
    return 0;
}
void NetUi::CopyGameSettings_51C7F0(NetworkGameSettings*)
{
}
char_type* NetUi::GetMapName_51CA10()
{
    static char_type sEmpty[1];
    return sEmpty;
}
char_type* NetUi::GetMapStyName_51CA50()
{
    return GetMapName_51CA10();
}
char_type* NetUi::GetMapScrName_51CA90()
{
    return GetMapName_51CA10();
}
