// The in-game pause menu: Escape pauses the game and opens it over the original's pause banner, with
// the options menu's settings applied at once, the controls, and quit.
#pragma once

#include "Player.hpp"

namespace PortPauseMenu
{
bool IsOpen();

// A key pressed in game (DIK code); true when the menu took it.
bool HandleKey(s32 action, Player* pPlayer);

// Drawn with the HUD.
void Draw();
} // namespace PortPauseMenu
