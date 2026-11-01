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

// The controls, for the pause menu: names, current bindings' names, and rebinding (column 0 the
// keyboard, 1 the gamepad; a binding in use elsewhere moves to the control being changed).
s32 NumControls();
const wchar_t* ControlName(s32 control);
const wchar_t* TextSpeedName(s32 speed);
void BindingNames(s32 control, wchar_t* pKey, wchar_t* pPad);
void SetBinding(s32 column, s32 control, s32 binding);
} // namespace PortOptionsMenu
