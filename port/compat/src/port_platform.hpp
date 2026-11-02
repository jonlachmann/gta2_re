// Links between the Win32 compat layer, the input layer and the renderer.
#pragma once

#include <windows.h>
#include <SDL3/SDL.h>

// Resolves a game path to the host file system; out must hold PATH_MAX bytes.
const char* port_resolve_path(const char* path, char* out);

SDL_Window* port_GetSDLWindow(HWND h);
void port_SetFullscreen(HWND h, bool fullscreen);
SDL_Window* port_MainSDLWindow();

// The player's window size (gta2_registry.ini), and the game resolution: the window's or, in full
// screen, the desktop's.
void port_GetWindowSize(int* pW, int* pH);
void port_SaveWindowSize(int w, int h);
void port_UpdateGameResolution();

// Test frames so far: Vid_FlipBuffers calls, except that while the game runs its logic steps count
// instead of its frames, so scripted tests do the same at any frame rate.
unsigned port_FrameCount();

// Scripted key presses and exit (GTA2_INPUT, GTA2_EXIT_AT), checked as the message loop pumps.
void port_InitTestInput();
void port_InjectTestInput(unsigned frame);

// The gamepad (pad.cpp). Pad binding codes: 0 to 11 the buttons (A, B, X, Y, LB, RB, Back, Start,
// left and right stick press, LT, RT), then the four directions of the stick and d-pad.
enum
{
    kPadLeft = 224,
    kPadRight = 225,
    kPadUp = 226,
    kPadDown = 227,
};
void port_OpenGamepad();
bool port_PadMode(); // the gamepad gave the last input
int port_GetPadBinding(int control);
void port_SetPadBinding(int control, int code);
int port_DefaultPadBinding(int control);
void port_CaptureStart(); // then the next key (DIK code) or pad code pressed, -1 until then
int port_CapturedKey();
int port_CapturedPad();
void port_AddPadMenuKeys(BYTE* pKeys);
void port_PadEvent(const SDL_Event& e);
void port_UpdatePad();

// Keyboard events for the game (dinput.cpp).
DWORD port_DikFromScancode(SDL_Scancode sc);
void port_PushKey(DWORD dik, bool down);

// Game state the gamepad needs (port/src/game_hooks.cpp).
bool port_GameRunning();
bool port_QuitMessageShown(); // or the pause menu
bool port_FrontendActive();

// Every SDL event, as the message loop pumps them.
void port_OnInputEvent(const SDL_Event& e);

#include "../../renderer/port_exports.hpp"

// Exports of a DLL the game loads by name, linked into the executable instead.
void port_RegisterDll(const char* name, const PortExport* pExports, int count);
