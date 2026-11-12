// The in-game pause menu: Escape pauses the game and opens the settings menu (menu.hpp), with RESUME and
// QUIT GAME, below lines of the original's pause screen.
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

// While the menu draws the original's pause screen lines (Hud_PauseScreen::DrawPause): true, and where
// a position of the HUD layout goes on screen.
bool IsDrawingStats();
f32 PlaceUi(f32 pos, bool y);
} // namespace PortPauseMenu
