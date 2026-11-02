// The gamepad. The game never sees it as a device: in game, each control's pad binding (pad_<n> in
// the registry's Control key, next to the keyboard's <n>) presses that control's keyboard key, so
// both work at once with the game's keyboard code; Start opens the pause menu (Escape), which the
// d-pad, A and B drive, as they answer the quit message. In the frontend it drives the menus as the
// arrows, Enter and Escape.
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <dinput.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "port_platform.hpp"

namespace
{
const int kNumControls = 12;
const DWORD kDefaultKeys[kNumControls] = {DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_LCONTROL, DIK_RETURN,
                                          DIK_SPACE, DIK_Z, DIK_X, DIK_TAB, DIK_LMENU, DIK_RSHIFT};
// Forward, backward, left, right, attack, enter/exit, jump/handbrake, previous and next weapon,
// special 1 to 3.
const int kDefaultPad[kNumControls] = {kPadUp, kPadDown, kPadLeft, kPadRight, 0, 3, 1, 4, 5, 2, 10, 11};
const Sint16 kThreshold = 24000;

SDL_Gamepad* sGamepad;
enum Mode
{
    kKeyboardMode,
    kPadMode,
    kMouseMode,
};
Mode sMode; // the device that gave the last input
bool sHeld[kNumControls];
DWORD sHeldKey[kNumControls];
bool sPrevCode[256];
int sCapturedKey = -1;
int sCapturedPad = -1;
bool sMigrated;
bool sMenuBlocked; // back from a game with a button still down: the menus wait for its release

const SDL_GamepadButton kButtons[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                                      SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
                                      SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
                                      SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
                                      SDL_GAMEPAD_BUTTON_RIGHT_STICK};

bool Button(SDL_GamepadButton b)
{
    return sGamepad && SDL_GetGamepadButton(sGamepad, b);
}

Sint16 Axis(SDL_GamepadAxis a)
{
    return sGamepad ? SDL_GetGamepadAxis(sGamepad, a) : 0;
}

bool CodeDown(int code)
{
    if (code >= 0 && code < 10)
    {
        return Button(kButtons[code]);
    }
    switch (code)
    {
        case 10:
            return Axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kThreshold / 2;
        case 11:
            return Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kThreshold / 2;
        case kPadLeft:
            return Button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || Axis(SDL_GAMEPAD_AXIS_LEFTX) < -kThreshold;
        case kPadRight:
            return Button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || Axis(SDL_GAMEPAD_AXIS_LEFTX) > kThreshold;
        case kPadUp:
            return Button(SDL_GAMEPAD_BUTTON_DPAD_UP) || Axis(SDL_GAMEPAD_AXIS_LEFTY) < -kThreshold;
        case kPadDown:
            return Button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || Axis(SDL_GAMEPAD_AXIS_LEFTY) > kThreshold;
        default:
            return false;
    }
}

bool OpenControlKey(HKEY* phKey)
{
    return RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Control", 0, NULL, 0, KEY_ALL_ACCESS,
                           NULL, phKey, NULL) == ERROR_SUCCESS;
}

bool ReadValue(HKEY hKey, const char* name, DWORD* pValue)
{
    DWORD size = sizeof(*pValue);
    return RegQueryValueExA(hKey, name, NULL, NULL, reinterpret_cast<BYTE*>(pValue), &size) == ERROR_SUCCESS;
}

void WriteValue(HKEY hKey, const char* name, DWORD value)
{
    RegSetValueExA(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

// GTA2 Manager stored a pad binding in the control's own value (bit 15 set); it moves to pad_<n>.
void MigrateBindings()
{
    sMigrated = true;
    HKEY hKey;
    if (!OpenControlKey(&hKey))
    {
        return;
    }
    for (int i = 0; i < kNumControls; i++)
    {
        char name[8];
        char padName[8];
        sprintf(name, "%d", i);
        sprintf(padName, "pad_%d", i);
        DWORD value;
        if (ReadValue(hKey, name, &value) && (value & 0x8000))
        {
            WriteValue(hKey, padName, value & 0xFF);
            WriteValue(hKey, name, kDefaultKeys[i]);
        }
    }
    RegCloseKey(hKey);
}

DWORD KeyFor(int control)
{
    HKEY hKey;
    DWORD value = kDefaultKeys[control];
    if (OpenControlKey(&hKey))
    {
        char name[8];
        sprintf(name, "%d", control);
        ReadValue(hKey, name, &value);
        RegCloseKey(hKey);
    }
    return value & 0xFF;
}

void Press(int control, bool down)
{
    if (sHeld[control] == down)
    {
        return;
    }
    sHeld[control] = down;
    if (down)
    {
        sHeldKey[control] = KeyFor(control);
    }
    port_PushKey(sHeldKey[control], down);
}

void Tap(DWORD dik)
{
    port_PushKey(dik, true);
    port_PushKey(dik, false);
}
} // namespace

// The first gamepad, or the first whose name contains $GTA2_GAMEPAD.
void port_OpenGamepad()
{
    if (sGamepad)
    {
        return;
    }
    const char* wanted = getenv("GTA2_GAMEPAD");
    int count = 0;
    SDL_JoystickID* pIds = SDL_GetGamepads(&count);
    for (int i = 0; pIds && i < count; i++)
    {
        const char* name = SDL_GetGamepadNameForID(pIds[i]);
        if (!wanted || (name && SDL_strcasestr(name, wanted)))
        {
            sGamepad = SDL_OpenGamepad(pIds[i]);
            break;
        }
    }
    SDL_free(pIds);
}

bool port_PadMode()
{
    return sMode == kPadMode;
}

bool port_MouseMode()
{
    return sMode == kMouseMode;
}

int port_GetPadBinding(int control)
{
    if (!sMigrated)
    {
        MigrateBindings();
    }
    HKEY hKey;
    DWORD value = static_cast<DWORD>(kDefaultPad[control]);
    if (OpenControlKey(&hKey))
    {
        char name[8];
        sprintf(name, "pad_%d", control);
        ReadValue(hKey, name, &value);
        RegCloseKey(hKey);
    }
    return static_cast<int>(value);
}

void port_SetPadBinding(int control, int code)
{
    HKEY hKey;
    if (OpenControlKey(&hKey))
    {
        char name[8];
        sprintf(name, "pad_%d", control);
        WriteValue(hKey, name, static_cast<DWORD>(code));
        RegCloseKey(hKey);
    }
}

int port_DefaultPadBinding(int control)
{
    return kDefaultPad[control];
}

void port_CaptureStart()
{
    sCapturedKey = -1;
    sCapturedPad = -1;
}

int port_CapturedKey()
{
    return sCapturedKey;
}

int port_CapturedPad()
{
    return sCapturedPad;
}

void port_AddPadMenuKeys(BYTE* pKeys)
{
    if (sMenuBlocked)
    {
        return;
    }
    const struct
    {
        bool mDown;
        DWORD mDik;
    } keys[] = {
        {CodeDown(kPadUp), DIK_UP},
        {CodeDown(kPadDown), DIK_DOWN},
        {CodeDown(kPadLeft), DIK_LEFT},
        {CodeDown(kPadRight), DIK_RIGHT},
        {Button(SDL_GAMEPAD_BUTTON_SOUTH) || Button(SDL_GAMEPAD_BUTTON_START), DIK_RETURN},
        {Button(SDL_GAMEPAD_BUTTON_EAST) || Button(SDL_GAMEPAD_BUTTON_BACK), DIK_ESCAPE},
        {Button(SDL_GAMEPAD_BUTTON_WEST), DIK_DELETE},
    };
    for (const auto& k : keys)
    {
        if (k.mDown)
        {
            pKeys[k.mDik] = 0x80;
        }
    }
}

void port_PadEvent(const SDL_Event& e)
{
    switch (e.type)
    {
        case SDL_EVENT_KEY_DOWN:
            if (!e.key.repeat)
            {
                sMode = kKeyboardMode;
                sCapturedKey = static_cast<int>(port_DikFromScancode(e.key.scancode));
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            port_OpenGamepad();
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (sGamepad && SDL_GetGamepadID(sGamepad) == e.gdevice.which)
            {
                SDL_CloseGamepad(sGamepad);
                sGamepad = NULL;
                port_OpenGamepad();
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (sGamepad && e.gbutton.which == SDL_GetGamepadID(sGamepad))
            {
                sMode = kPadMode;
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (sGamepad && e.gaxis.which == SDL_GetGamepadID(sGamepad) && (e.gaxis.value > kThreshold || e.gaxis.value < -kThreshold))
            {
                sMode = kPadMode;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (e.motion.xrel != 0 || e.motion.yrel != 0)
            {
                sMode = kMouseMode;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
            sMode = kMouseMode;
            break;
        default:
            break;
    }
}

void port_UpdatePad()
{
    // New presses, for binding capture (Start stays the pause button).
    int codes[kNumControls + 4];
    int n = 0;
    for (int c = 0; c < 12; c++)
    {
        codes[n++] = c;
    }
    for (int c = kPadLeft; c <= kPadDown; c++)
    {
        codes[n++] = c;
    }
    for (int i = 0; i < n; i++)
    {
        const int code = codes[i];
        const bool down = CodeDown(code);
        if (down && !sPrevCode[code] && code != 7 && sCapturedPad < 0)
        {
            sCapturedPad = code;
        }
        sPrevCode[code] = down;
    }

    const bool start = Button(SDL_GAMEPAD_BUTTON_START);
    const bool a = Button(SDL_GAMEPAD_BUTTON_SOUTH);
    const bool b = Button(SDL_GAMEPAD_BUTTON_EAST);
    static bool sPrevStart;
    static bool sPrevA;
    static bool sPrevB;

    const bool inGame = port_GameRunning();
    static bool sWasInGame;
    if (sWasInGame && !inGame)
    {
        sMenuBlocked = true;
    }
    sWasInGame = inGame;
    if (sMenuBlocked)
    {
        bool anyDown = false;
        for (int i = 0; i < n; i++)
        {
            anyDown |= CodeDown(codes[i]);
        }
        sMenuBlocked = anyDown;
    }
    const bool quitShown = inGame && port_QuitMessageShown();
    for (int i = 0; i < kNumControls; i++)
    {
        Press(i, inGame && !quitShown && CodeDown(port_GetPadBinding(i)));
    }
    if (inGame)
    {
        if (start && !sPrevStart)
        {
            Tap(DIK_ESCAPE);
        }
        if (quitShown && a && !sPrevA)
        {
            Tap(DIK_RETURN);
        }
        if (quitShown && b && !sPrevB)
        {
            Tap(DIK_ESCAPE);
        }
        // The pause menu's navigation.
        static bool sPrevDir[4];
        const int dirs[4] = {kPadUp, kPadDown, kPadLeft, kPadRight};
        const DWORD keys[4] = {DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT};
        for (int d = 0; d < 4; d++)
        {
            const bool down = CodeDown(dirs[d]);
            if (quitShown && down && !sPrevDir[d])
            {
                Tap(keys[d]);
            }
            sPrevDir[d] = down;
        }
    }
    sPrevStart = start;
    sPrevA = a;
    sPrevB = b;
}
