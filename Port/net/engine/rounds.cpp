// Port/net (docs/netcode.md, "Rounds"): a match as rounds on one map. The game ends a network game at its frag
// limit (PlayerScoreTracker: frags >= the limit) and its time limit (ManageNetworkTimeAndHudPager_4DADA0), which a
// dedicated server can't come back from; so the game gets neither (StartNetworkGame) and the server ends a round
// itself (session/server.cpp), here: scores, weapons and power-ups gone, everyone at a restart zone.
#include "internal.hpp"
#include "Source/GameSession.hpp"
#include "Source/Hud.hpp"
#include "Source/Player.hpp"
#include "Source/hud_message_priority.hpp"
#include <string.h>

net::engine::Round& net::engine::CurrentRound()
{
    static Round round;
    return round;
}

void net::engine::NewRound()
{
    for (int i = 0; i < PlayerCount(); i++)
    {
        Player* p = InGamePed(i) ? GetPlayer(i) : NULL;
        if (!p)
        {
            continue;
        }
        p->UnloadCarWeapons_564C00();
        p->RemovePlayerWeapons_564C50();
        p->ClearPowerUpsExceptJailCard_564CF0();
        ResetDeaths(i);
        PutInGame(i, true); // a fresh score, at the next restart zone
    }
}

void net::engine::ShowHudMessage(const char* text)
{
    static wchar_t wide[128]; // the HUD keeps the pointer while it shows it
    size_t n = 0;
    for (; text[n] && n + 1 < sizeof(wide) / sizeof(wide[0]); n++)
    {
        wide[n] = (wchar_t)(unsigned char)text[n];
    }
    wide[n] = 0;
    if (gHud_2B00_706620)
    {
        gHud_2B00_706620->field_111C_message.ShowMessage_5D1A00(wide, hud_message_priority::important_3);
    }
}
