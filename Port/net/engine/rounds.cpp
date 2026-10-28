// Port/net (docs/netcode.md, "Playing"): a match as rounds on one map. The game ends a network game at its frag
// limit (PlayerScoreTracker: frags >= the limit) and its time limit (ManageNetworkTimeAndHudPager_4DADA0), which a
// dedicated server can't come back from; so the game gets neither (StartNetworkGame) and the server ends a round
// itself (session/server.cpp), here: scores, weapons and power-ups gone, everyone at a restart zone.
#include "internal.hpp"
#include "Source/Game.hpp"
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
    for (int i = 0; i < PlayerCount() && i < MAX_PLAYERS; i++)
    {
        // Every slot's score (a new map's game starts before the map script made the players' peds)
        gGameSession_67E8E0.field_490_frags_list[i] = 0;
        gGameSession_67E8E0.field_49C_points_list[i] = 0;
        ResetDeaths(i);
        Player* p = InGamePed(i) ? GetPlayer(i) : NULL;
        if (p)
        {
            p->UnloadCarWeapons_564C00();
            p->RemovePlayerWeapons_564C50();
            p->ClearPowerUpsExceptJailCard_564CF0();
            PutInGame(i, true); // at the next restart zone
        }
    }
}

namespace
{
int gExitTicks; // the pause left before a map change ends the game
} // namespace

void net::engine::ChangeMap(const std::string& gmp, const std::string& sty, const std::string& scr, int pauseTicks)
{
    SetNextMap(gmp, sty, scr);
    gExitTicks = pauseTicks;
    KeepMapChange();
}

// The game's exit timer, set again every frame: a client's restore brings back a game that wasn't ending
void net::engine::KeepMapChange()
{
    if (MapChangePending() && gGame_0x40_67E008)
    {
        gGame_0x40_67E008->ExitGameNoBonus_4B8C00(gExitTicks, GameExitType::MultiplayerExit_5); // only ever sooner
        gExitTicks = gExitTicks > 1 ? gExitTicks - 1 : 1;
    }
}

void net::engine::ResetForNewGame()
{
    ClearReplicas();
    ClearPickups();
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        ResetDeaths(i);
    }
}
