// The in-game pause menu: Escape pauses the game and opens the settings menu (menu.hpp) over the
// original's pause banner, with RESUME and QUIT GAME.
#pragma once

#include "Player.hpp"

namespace PortPauseMenu
{
bool IsOpen();

// Loads what the menu shows and plays as a city starts (fstyle.sty takes a quarter of a second on the
// 3DS), and frees it as the city ends.
void Preload();
void Release();

// A key pressed in game (DIK code); true when the menu took it.
bool HandleKey(s32 action, Player* pPlayer);

// Drawn with the HUD.
void Draw();
} // namespace PortPauseMenu
