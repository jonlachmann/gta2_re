// The in-game pause menu: Escape pauses the game and opens the settings menu (menu.hpp) over the
// original's pause banner, with RESUME and QUIT GAME.
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
