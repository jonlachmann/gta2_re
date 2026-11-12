// The settings menu (menu.hpp) in the frontend, on a page the original leaves unused (12), reached
// from the start menu's "Options" in place of the external GTA2 Manager.
#pragma once

#include "Frontend.hpp"

namespace PortOptionsMenu
{
// The start menu's corner line, our version in place of the game's. The frontend fonts have no lower case.
void VersionText(wchar_t* pOut);

// After the original pages are set up.
void Setup(Frontend* pFrontend);

// Input and state of the menu's page; false when another page is current.
bool Update(Frontend* pFrontend);

// After the frontend drew the page.
void Draw(Frontend* pFrontend);

// The player name typed with the gamepad: up and down change the last letter, right adds one, left
// removes one, A keeps the name, B cancels. False when the keyboard is in use.
bool UpdateNameEntry(Frontend* pFrontend);

// The frontend's key hints (ENTER, ESC, DEL, BACKSPACE) as the gamepad's buttons once it was used last.
void PadKeyHint(u8 hint, wchar_t* pText);
} // namespace PortOptionsMenu
