// In-game settings, in place of the external GTA2 Manager: frontend pages the original leaves unused
// (12, 13, 15, 16), reached from the start menu's "Options".
#pragma once

#include "Frontend.hpp"

namespace PortOptionsMenu
{
// After the original pages are set up.
void Setup(Frontend* pFrontend);

// Input and state of our pages; false when another page is current.
bool Update(Frontend* pFrontend);

// Text of a left/right option of our pages; false for the original's.
bool GetOptionText(Frontend* pFrontend, u16 page, u16 option, wchar_t** ppText);

// The player name typed with the gamepad: up and down change the last letter, right adds one, left
// removes one, A keeps the name, B cancels. False when the keyboard is in use.
bool UpdateNameEntry(Frontend* pFrontend);

// The frontend's key hints (ENTER, ESC, DEL, BACKSPACE) as the gamepad's buttons once it was used last.
void PadKeyHint(u8 hint, wchar_t* pText);
} // namespace PortOptionsMenu
