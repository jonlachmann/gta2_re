// The gamepads, all of them at once: a button counts if held on any, the sticks and triggers are the ones
// of the pad that gave the last input. The game never sees them as devices: in game, each control's pad binding (pad_<n> in
// the registry's Control key, next to the keyboard's <n>) presses that control's keyboard key, so
// both work at once with the game's keyboard code; Start opens the pause menu (Escape), which the
// d-pad, A and B drive, as they answer the quit message. In the frontend it drives the menus as the
// arrows, Enter and Escape. With TWIN STICK on foot, the sticks move and aim (port_TwinStickInput) and RT
// attacks. Each control's second key (alt_<n>), a key, mouse button or wheel notch, presses it the same way.
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <dinput.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port_platform.hpp"

namespace
{
const int kNumControls = 12;
const DWORD kDefaultKeys[kNumControls] = {DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_LCONTROL, DIK_RETURN,
                                          DIK_SPACE, DIK_Z, DIK_X, DIK_TAB, DIK_LMENU, DIK_RSHIFT};
// Forward, backward, left, right, attack, enter/exit, jump/handbrake, previous and next weapon,
// special 1 to 3.
const int kDefaultPad[kNumControls] = {kPadUp, kPadDown, kPadLeft, kPadRight, 0, 3, 1, 4, 5, 2, 10, 11};
// W, S, A, D and F by position.
const int kDefaultAlt[kNumControls] = {DIK_W, DIK_S, DIK_A, DIK_D, kMouseCode + SDL_BUTTON_LEFT, DIK_F, 0,
                                       kWheelUpCode, kWheelDownCode, 0, 0, 0};
const Sint16 kThreshold = 24000;

const int kMaxGamepads = 8;
SDL_Gamepad* sGamepads[kMaxGamepads];
int sNumGamepads;
SDL_Gamepad* sActivePad; // the last to give an input
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
bool sMouseAims; // the mouse was used in game after the pad
bool sKeyDown[256]; // by DIK code
unsigned sButtonsDown; // bit n: SDL mouse button n
int sAlt[kNumControls];
bool sAltRead;
int sPad[kNumControls]; // pad_<n>, read once: port_UpdatePad wants them several times a frame
bool sPadRead;
int sWheelCode; // a wheel notch holds its control down for two steps
unsigned sWheelUntil;

const SDL_GamepadButton kButtons[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                                      SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
                                      SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
                                      SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
                                      SDL_GAMEPAD_BUTTON_RIGHT_STICK};

// While set (port_UpdatePad, where the pads cannot change), each button and axis is read from SDL once:
// every read takes SDL's locks, system calls on the 3DS.
bool sPadCache;
Uint32 sButtonRead, sButtonDown, sAxisRead;
Sint16 sAxisValue[SDL_GAMEPAD_AXIS_COUNT];
static_assert(SDL_GAMEPAD_BUTTON_COUNT <= 32, "one bit per button");

bool ReadButton(SDL_GamepadButton b)
{
    for (int i = 0; i < sNumGamepads; i++)
    {
        if (SDL_GetGamepadButton(sGamepads[i], b))
        {
            return true;
        }
    }
    return false;
}

bool Button(SDL_GamepadButton b)
{
    if (!sPadCache || b < 0 || b >= SDL_GAMEPAD_BUTTON_COUNT)
    {
        return ReadButton(b);
    }
    const Uint32 bit = 1u << b;
    if (!(sButtonRead & bit))
    {
        sButtonRead |= bit;
        sButtonDown = ReadButton(b) ? sButtonDown | bit : sButtonDown & ~bit;
    }
    return (sButtonDown & bit) != 0;
}

Sint16 Axis(SDL_GamepadAxis a)
{
    if (!sPadCache || a < 0 || a >= SDL_GAMEPAD_AXIS_COUNT)
    {
        return sActivePad ? SDL_GetGamepadAxis(sActivePad, a) : 0;
    }
    if (!(sAxisRead & (1u << a)))
    {
        sAxisRead |= 1u << a;
        sAxisValue[a] = sActivePad ? SDL_GetGamepadAxis(sActivePad, a) : 0;
    }
    return sAxisValue[a];
}

// The pad's input makes it the active one, if it is one of ours.
bool PadInput(SDL_JoystickID id)
{
    SDL_Gamepad* pPad = SDL_GetGamepadFromID(id);
    for (int i = 0; i < sNumGamepads; i++)
    {
        if (sGamepads[i] == pPad)
        {
            sActivePad = pPad;
            return true;
        }
    }
    return false;
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

// format: the value's name, with %d for the control.
DWORD ReadControl(const char* format, int control, DWORD value)
{
    HKEY hKey;
    if (OpenControlKey(&hKey))
    {
        char name[16];
        sprintf(name, format, control);
        ReadValue(hKey, name, &value);
        RegCloseKey(hKey);
    }
    return value;
}

void WriteControl(const char* format, int control, DWORD value)
{
    HKEY hKey;
    if (OpenControlKey(&hKey))
    {
        char name[16];
        sprintf(name, format, control);
        WriteValue(hKey, name, value);
        RegCloseKey(hKey);
    }
}

DWORD KeyFor(int control)
{
    return ReadControl("%d", control, kDefaultKeys[control]) & 0xFF;
}

bool AltDown(int control)
{
    const int code = port_GetAltBinding(control);
    if (code > 0 && code < 256)
    {
        return sKeyDown[code];
    }
    if (code > kMouseCode && code < kWheelUpCode)
    {
        return (sButtonsDown >> (code - kMouseCode)) & 1;
    }
    return code >= kWheelUpCode && code == sWheelCode && port_FrameCount() < sWheelUntil;
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

// In a vehicle the triggers and the stick's X count past these, and give the physics how far past.
const Sint16 kTriggerDeadZone = 2600;
const Sint16 kStickDeadZone = 8192;
float sCarForward = 1.0f;
float sCarBackward = 1.0f;
float sCarSteer = 1.0f;

// 0 at the dead zone, 1 at the end of the travel.
float Beyond(Sint16 value, Sint16 deadZone)
{
    const float t = ((value < 0 ? -value : value) - deadZone) / static_cast<float>(32767 - deadZone);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// In a vehicle: RT accelerates and LT brakes or reverses, as do the d-pad's up and down, and the stick only
// steers. The triggers' own controls give them up, SPECIAL 2 (the camera pan) to L3 if no control uses it.
bool CarPadDown(int control, int code, bool l3Free)
{
    if (control < 2)
    {
        return Button(control ? SDL_GAMEPAD_BUTTON_DPAD_DOWN : SDL_GAMEPAD_BUTTON_DPAD_UP) ||
            Axis(control ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerDeadZone;
    }
    if (code == kPadLeft || code == kPadRight)
    {
        const Sint16 x = Axis(SDL_GAMEPAD_AXIS_LEFTX);
        return Button(code == kPadLeft ? SDL_GAMEPAD_BUTTON_DPAD_LEFT : SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
            (code == kPadLeft ? x < -kStickDeadZone : x > kStickDeadZone);
    }
    if (code == 10 || code == 11)
    {
        return control == 10 && l3Free && CodeDown(8);
    }
    return CodeDown(code);
}

// A trigger's or the stick's part of the way, 1 while a key or the d-pad presses the control too.
float CarAnalog(bool full, Sint16 value, Sint16 deadZone)
{
    return full || value <= deadZone ? 1.0f : Beyond(value, deadZone);
}

// A stick within its dead zone (a quarter of its range) is centred.
void Stick(SDL_GamepadAxis x, SDL_GamepadAxis y, float* pX, float* pY)
{
    *pX = Axis(x) / 32768.0f;
    *pY = Axis(y) / 32768.0f;
    if (*pX * *pX + *pY * *pY < 0.25f * 0.25f)
    {
        *pX = *pY = 0.0f;
    }
}
} // namespace

bool port_TwinStickInput(float* pMoveX, float* pMoveY, float* pAimX, float* pAimY)
{
    Stick(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, pMoveX, pMoveY);
    Stick(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, pAimX, pAimY);
    *pMoveX += (Button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || AltDown(3)) - (Button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || AltDown(2));
    *pMoveY += (Button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || AltDown(1)) - (Button(SDL_GAMEPAD_BUTTON_DPAD_UP) || AltDown(0));
    return sMouseAims;
}

void port_PadVehicleInput(float* pForward, float* pBackward, float* pSteer)
{
    *pForward = sCarForward;
    *pBackward = sCarBackward;
    *pSteer = sCarSteer;
}

// TWIN STICK on foot, with SPECIAL 2 on its default LT: LT looks further instead.
static bool LtLooks()
{
    return port_TwinStick() == 1 && port_GetPadBinding(10) == 10;
}

bool port_LookFurther()
{
    bool r3 = CodeDown(9);
    for (int i = 0; i < kNumControls; i++)
    {
        r3 = r3 && port_GetPadBinding(i) != 9;
    }
    return sKeyDown[DIK_LSHIFT] || r3 || (LtLooks() && CodeDown(10));
}

// The gamepads not open yet, or only those whose name contains $GTA2_GAMEPAD.
void port_OpenGamepad()
{
    const char* wanted = getenv("GTA2_GAMEPAD");
    int count = 0;
    SDL_JoystickID* pIds = SDL_GetGamepads(&count);
    for (int i = 0; pIds && i < count && sNumGamepads < kMaxGamepads; i++)
    {
        const char* name = SDL_GetGamepadNameForID(pIds[i]);
        if (!SDL_GetGamepadFromID(pIds[i]) && (!wanted || (name && SDL_strcasestr(name, wanted))))
        {
            if (SDL_Gamepad* pPad = SDL_OpenGamepad(pIds[i]))
            {
                sGamepads[sNumGamepads++] = pPad;
                sActivePad = sActivePad ? sActivePad : pPad;
            }
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
    if (!sPadRead)
    {
        if (!sMigrated)
        {
            MigrateBindings();
        }
        sPadRead = true;
        for (int i = 0; i < kNumControls; i++)
        {
            sPad[i] = static_cast<int>(ReadControl("pad_%d", i, static_cast<DWORD>(kDefaultPad[i])));
        }
    }
    return sPad[control];
}

void port_SetPadBinding(int control, int code)
{
    port_GetPadBinding(control);
    sPad[control] = code;
    WriteControl("pad_%d", control, static_cast<DWORD>(code));
}

int port_DefaultPadBinding(int control)
{
    return kDefaultPad[control];
}

int port_GetAltBinding(int control)
{
    if (!sAltRead)
    {
        sAltRead = true;
        for (int i = 0; i < kNumControls; i++)
        {
            sAlt[i] = static_cast<int>(ReadControl("alt_%d", i, static_cast<DWORD>(kDefaultAlt[i])));
        }
    }
    return sAlt[control];
}

void port_SetAltBinding(int control, int code)
{
    port_GetAltBinding(control);
    sAlt[control] = code;
    WriteControl("alt_%d", control, static_cast<DWORD>(code));
}

int port_DefaultAltBinding(int control)
{
    return kDefaultAlt[control];
}

bool port_AltKeyInGame(DWORD dik)
{
    if (!port_GameRunning() || port_QuitMessageShown())
    {
        return false;
    }
    bool alt = false;
    for (int i = 0; i < kNumControls; i++)
    {
        if (KeyFor(i) == dik)
        {
            return false;
        }
        alt = alt || port_GetAltBinding(i) == static_cast<int>(dik);
    }
    return alt;
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
        case SDL_EVENT_KEY_UP:
        {
            const DWORD dik = port_DikFromScancode(e.key.scancode);
            sKeyDown[dik] = dik && e.key.down;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
            {
                sMode = kKeyboardMode;
                sCapturedKey = static_cast<int>(dik);
            }
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            memset(sKeyDown, 0, sizeof(sKeyDown));
            sButtonsDown = 0;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            port_OpenGamepad();
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            for (int i = 0; i < sNumGamepads; i++)
            {
                if (SDL_GetGamepadID(sGamepads[i]) == e.gdevice.which)
                {
                    SDL_Gamepad* pPad = sGamepads[i];
                    sGamepads[i] = sGamepads[--sNumGamepads];
                    sActivePad = sActivePad == pPad ? (sNumGamepads ? sGamepads[0] : NULL) : sActivePad;
                    SDL_CloseGamepad(pPad);
                    break;
                }
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (PadInput(e.gbutton.which))
            {
                sMode = kPadMode;
                sMouseAims = false;
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if ((e.gaxis.value > kThreshold || e.gaxis.value < -kThreshold) && PadInput(e.gaxis.which))
            {
                sMode = kPadMode;
                sMouseAims = false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (e.motion.xrel != 0 || e.motion.yrel != 0)
            {
                sMode = kMouseMode;
                sMouseAims = sMouseAims || port_GameRunning();
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (e.wheel.y != 0.0f)
            {
                const int code = (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y) > 0 ? kWheelUpCode : kWheelDownCode;
                sCapturedKey = code;
                if (port_GameRunning() && port_FrameCount() > sWheelUntil)
                {
                    sWheelCode = code;
                    sWheelUntil = port_FrameCount() + 2;
                }
            }
            sMode = kMouseMode;
            sMouseAims = sMouseAims || port_GameRunning();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            sButtonsDown |= 1u << (e.button.button & 31);
            sCapturedKey = kMouseCode + e.button.button;
            sMode = kMouseMode;
            sMouseAims = sMouseAims || port_GameRunning();
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            sButtonsDown &= ~(1u << (e.button.button & 31));
            break;
        default:
            break;
    }
}

void port_UpdatePad()
{
    struct PadCache
    {
        PadCache()
        {
            sPadCache = true;
            sButtonRead = 0;
            sAxisRead = 0;
        }
        ~PadCache() { sPadCache = false; }
    } cache;
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
    const int twin = inGame && !quitShown ? port_TwinStick() : 0;
    // The vehicle's triggers, unless FORWARD or BACKWARD has a pad input of the player's choice.
    const bool car = inGame && !quitShown && port_Driving() && port_GetPadBinding(0) == kPadUp && port_GetPadBinding(1) == kPadDown;
    bool l3Free = true;
    for (int i = 0; i < kNumControls; i++)
    {
        l3Free = l3Free && port_GetPadBinding(i) != 8;
    }
    const bool lt = LtLooks();
    // TWIN STICK in a vehicle with a roof gun: RB fires, so the thumb stays on the aiming stick.
    const bool rbFires = twin == 2 && port_TwinStickAims() && port_GetPadBinding(8) == 5;
    const auto key = [](int control) { return sKeyDown[KeyFor(control)] || AltDown(control); };
    const Sint16 x = Axis(SDL_GAMEPAD_AXIS_LEFTX);
    const bool steerFull = key(2) || key(3) || Button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || Button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    const float steer = CarAnalog(steerFull, x < 0 ? static_cast<Sint16>(-(x + 1)) : x, kStickDeadZone);
    sCarForward = CarAnalog(!car || key(0) || Button(SDL_GAMEPAD_BUTTON_DPAD_UP), Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), kTriggerDeadZone);
    sCarBackward = CarAnalog(!car || key(1) || Button(SDL_GAMEPAD_BUTTON_DPAD_DOWN), Axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), kTriggerDeadZone);
    // From three fifths of the wheels' turn: less, a car at town speeds slides sideways without turning.
    sCarSteer = car && steer < 1.0f ? 0.6f + 0.4f * steer : 1.0f;
    for (int i = 0; i < kNumControls; i++)
    {
        // TWIN STICK on foot: the stick, d-pad and movement second keys move (port_TwinStickInput), RT attacks.
        const int code = port_GetPadBinding(i);
        const bool move = twin == 1 && i < 4;
        const bool pad = (car ? CarPadDown(i, code, l3Free) : CodeDown(code) && !(twin == 1 && (code == 11 || (code == 10 && lt)))) &&
            !(rbFires && code == 5);
        const bool down = !move && (pad || AltDown(i) || (twin == 1 && i == 4 && CodeDown(11)) || (rbFires && i == 4 && CodeDown(5)));
        Press(i, inGame && !quitShown && down);
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
