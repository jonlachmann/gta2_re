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

// The full screen mode chosen in the settings, NULL for the desktop's.
const SDL_DisplayMode* port_FullscreenMode();

// GTA2_SPEED (the game's clock that many times faster) and GTA2_HEADLESS (no window content, sound or gamepads).
double port_ClockSpeed();
bool port_Headless();

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
// Second key binding codes: a DIK code, or kMouseCode plus an SDL mouse button, or a wheel notch; 0 none.
enum
{
    kMouseCode = 0x100,
    kWheelUpCode = 0x110,
    kWheelDownCode = 0x111,
};
void port_OpenGamepad();
bool port_PadMode(); // the gamepad gave the last input
bool port_MouseMode(); // the mouse gave the last input
int port_GetPadBinding(int control);
void port_SetPadBinding(int control, int code);
int port_DefaultPadBinding(int control);
int port_GetAltBinding(int control);
void port_SetAltBinding(int control, int code);
int port_DefaultAltBinding(int control);
// In game, a second key (and no control's key) reaches the game only as its control's key: the game reads
// one key event per step.
bool port_AltKeyInGame(DWORD dik);
// The layout's letter on the key at that DIK code's position (dinput.cpp), 0 if not a letter.
int port_KeyLetter(int dik);
void port_CaptureStart(); // then the next key (DIK or mouse code) or pad code pressed, -1 until then
int port_CapturedKey();
int port_CapturedPad();
void port_AddPadMenuKeys(BYTE* pKeys);
void port_PadEvent(const SDL_Event& e);
void port_UpdatePad();
// TWIN STICK on foot: the left stick, d-pad and movement second keys as a move, the right stick as an aim,
// both screen-relative, y down, 0 within the dead zones. True if the mouse aims (it was used after the pad).
bool port_TwinStickInput(float* pMoveX, float* pMoveY, float* pAimX, float* pAimY);
// AIM CAMERA looks further: left Shift, R3 unless a control is bound to it, or LT on foot (pad.cpp).
bool port_LookFurther();

// The mouse for the menus (mouse.cpp): the position in the game's frame and the clicks and wheel
// notches since the last call, true if it moved; what the frame leaves is dropped at its end.
void port_MouseEvent(const SDL_Event& e);
bool port_TakeMouse(float* pX, float* pY, int* pLeft, int* pRight, int* pWheel);
void port_MouseEndFrame();
void port_SetGameCursor(); // cursor.cpp
// The left button is held, for dragging.
bool port_MouseLeftDown();
// The position in the game's frame.
void port_MousePos(float* pX, float* pY);

// The size of the frame the game draws (video_port.cpp).
void port_FrameSize(int* pW, int* pH);

// Keyboard events for the game (dinput.cpp).
DWORD port_DikFromScancode(SDL_Scancode sc);
void port_PushKey(DWORD dik, bool down);

// Game state the gamepad needs (port/src/game_hooks.cpp).
bool port_GameRunning();
bool port_QuitMessageShown(); // or the pause menu
bool port_FrontendActive();
bool port_MenuUsesMouse(); // the frontend or the pause menu
// TWIN STICK and the local player with the controls on: 1 on foot, 2 in a vehicle, else 0.
int port_TwinStick();
// TWIN STICK aims: on foot, or in a vehicle with a roof gun.
bool port_TwinStickAims();
// The local player with the controls on, in a vehicle, in either CONTROLS style.
bool port_Driving();
// The local player on his way into a vehicle (walking to it or getting in).
bool port_EnteringCar();
// The player's vehicle: how far RT, LT and the stick press FORWARD, BACKWARD and the steering, 0 to 1, 1 for
// a key or the d-pad (pad.cpp).
void port_PadVehicleInput(float* pForward, float* pBackward, float* pSteer);

// Every SDL event, as the message loop pumps them.
void port_OnInputEvent(const SDL_Event& e);

#include "../../renderer/port_exports.hpp"

// Exports of a DLL the game loads by name, linked into the executable instead.
void port_RegisterDll(const char* name, const PortExport* pExports, int count);
