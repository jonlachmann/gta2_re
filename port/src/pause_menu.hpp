// The in-game pause menu: Escape pauses the game and opens it (resume, sound, screen, text speed,
// controls, quit) over the original's pause banner. Quit leads to the original's quit message.
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
