// Game state the platform layer asks about (declared in compat/src/port_platform.hpp).
#include "Frontend.hpp"
#include "Game.hpp"
#include "Player.hpp"

bool port_GameRunning()
{
    return gGame_0x40 && !gFrontend;
}

bool port_QuitMessageShown()
{
    return port_GameRunning() && gGame_0x40->mLocalPlayer && gGame_0x40->mLocalPlayer->mShowQuitMessage;
}
