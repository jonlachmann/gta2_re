// The settings menu, the same in the frontend (options_menu.cpp) and in game (pause_menu.cpp): its
// pages, navigation, settings, key and pad bindings, and its drawing with the frontend's fonts.
#pragma once

#include "types.hpp"

namespace PortMenu
{
enum Input
{
    kUp,
    kDown,
    kLeft,
    kRight,
    kSelect,
    kCancel,
};

// What an input did, numbered as the frontend's sounds up to kWentBack.
enum Result
{
    kNothing,
    kMovedUp,
    kMovedDown,
    kLowered,
    kRaised,
    kEntered,
    kWentBack,
    kClosed, // Back on the main page, BACK there, or RESUME
    kQuitGame,
};

// The gamepad gave the last input.
bool PadMode();

// On the main page; in game it starts with RESUME and ends with QUIT GAME.
void Open();

Result HandleInput(Input input);

// While waiting for a key or pad input to bind: every frame, as no input goes to HandleInput.
bool IsCapturing();
Result UpdateCapture();

const wchar_t* Title();

// The 640x480 layout at that scale and screen position, on a black panel sized to the page if asked.
// Needs the frontend's style.
void Draw(f32 scale, f32 left, f32 top, bool panel);
} // namespace PortMenu
