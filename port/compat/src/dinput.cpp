// DirectInput on SDL3: the keyboard and the first gamepad as DirectInput devices, with DIK_* key codes
// and DIJOYSTATE offsets. Only real state changes are buffered (no key repeat, no lock key resyncs):
// the game reads one buffered event per frame.
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <dinput.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <deque>
#include <string.h>
#include <vector>
#include "port_platform.hpp"

#undef remove

namespace
{
const GUID kGamepadGuid = {0x47544132, 0x5044, 0x0001, {'S', 'D', 'L', 'P', 'A', 'D', 0, 1}};

DWORD Dik(SDL_Scancode sc)
{
    switch (sc)
    {
        case SDL_SCANCODE_ESCAPE: return DIK_ESCAPE;
        case SDL_SCANCODE_1: return DIK_1;
        case SDL_SCANCODE_2: return DIK_2;
        case SDL_SCANCODE_3: return DIK_3;
        case SDL_SCANCODE_4: return DIK_4;
        case SDL_SCANCODE_5: return DIK_5;
        case SDL_SCANCODE_6: return DIK_6;
        case SDL_SCANCODE_7: return DIK_7;
        case SDL_SCANCODE_8: return DIK_8;
        case SDL_SCANCODE_9: return DIK_9;
        case SDL_SCANCODE_0: return DIK_0;
        case SDL_SCANCODE_MINUS: return DIK_MINUS;
        case SDL_SCANCODE_EQUALS: return DIK_EQUALS;
        case SDL_SCANCODE_BACKSPACE: return DIK_BACK;
        case SDL_SCANCODE_TAB: return DIK_TAB;
        case SDL_SCANCODE_Q: return DIK_Q;
        case SDL_SCANCODE_W: return DIK_W;
        case SDL_SCANCODE_E: return DIK_E;
        case SDL_SCANCODE_R: return DIK_R;
        case SDL_SCANCODE_T: return DIK_T;
        case SDL_SCANCODE_Y: return DIK_Y;
        case SDL_SCANCODE_U: return DIK_U;
        case SDL_SCANCODE_I: return DIK_I;
        case SDL_SCANCODE_O: return DIK_O;
        case SDL_SCANCODE_P: return DIK_P;
        case SDL_SCANCODE_LEFTBRACKET: return DIK_LBRACKET;
        case SDL_SCANCODE_RIGHTBRACKET: return DIK_RBRACKET;
        case SDL_SCANCODE_RETURN: return DIK_RETURN;
        case SDL_SCANCODE_LCTRL: return DIK_LCONTROL;
        case SDL_SCANCODE_A: return DIK_A;
        case SDL_SCANCODE_S: return DIK_S;
        case SDL_SCANCODE_D: return DIK_D;
        case SDL_SCANCODE_F: return DIK_F;
        case SDL_SCANCODE_G: return DIK_G;
        case SDL_SCANCODE_H: return DIK_H;
        case SDL_SCANCODE_J: return DIK_J;
        case SDL_SCANCODE_K: return DIK_K;
        case SDL_SCANCODE_L: return DIK_L;
        case SDL_SCANCODE_SEMICOLON: return DIK_SEMICOLON;
        case SDL_SCANCODE_APOSTROPHE: return DIK_APOSTROPHE;
        case SDL_SCANCODE_GRAVE: return DIK_GRAVE;
        case SDL_SCANCODE_LSHIFT: return DIK_LSHIFT;
        case SDL_SCANCODE_BACKSLASH: return DIK_BACKSLASH;
        case SDL_SCANCODE_Z: return DIK_Z;
        case SDL_SCANCODE_X: return DIK_X;
        case SDL_SCANCODE_C: return DIK_C;
        case SDL_SCANCODE_V: return DIK_V;
        case SDL_SCANCODE_B: return DIK_B;
        case SDL_SCANCODE_N: return DIK_N;
        case SDL_SCANCODE_M: return DIK_M;
        case SDL_SCANCODE_COMMA: return DIK_COMMA;
        case SDL_SCANCODE_PERIOD: return DIK_PERIOD;
        case SDL_SCANCODE_SLASH: return DIK_SLASH;
        case SDL_SCANCODE_RSHIFT: return DIK_RSHIFT;
        case SDL_SCANCODE_KP_MULTIPLY: return DIK_MULTIPLY;
        case SDL_SCANCODE_LALT: return DIK_LMENU;
        case SDL_SCANCODE_SPACE: return DIK_SPACE;
        case SDL_SCANCODE_CAPSLOCK: return DIK_CAPITAL;
        case SDL_SCANCODE_F1: return DIK_F1;
        case SDL_SCANCODE_F2: return DIK_F2;
        case SDL_SCANCODE_F3: return DIK_F3;
        case SDL_SCANCODE_F4: return DIK_F4;
        case SDL_SCANCODE_F5: return DIK_F5;
        case SDL_SCANCODE_F6: return DIK_F6;
        case SDL_SCANCODE_F7: return DIK_F7;
        case SDL_SCANCODE_F8: return DIK_F8;
        case SDL_SCANCODE_F9: return DIK_F9;
        case SDL_SCANCODE_F10: return DIK_F10;
        case SDL_SCANCODE_F11: return DIK_F11;
        case SDL_SCANCODE_F12: return DIK_F12;
        case SDL_SCANCODE_KP_7: return DIK_NUMPAD7;
        case SDL_SCANCODE_KP_8: return DIK_NUMPAD8;
        case SDL_SCANCODE_KP_9: return DIK_NUMPAD9;
        case SDL_SCANCODE_KP_MINUS: return DIK_SUBTRACT;
        case SDL_SCANCODE_KP_4: return DIK_NUMPAD4;
        case SDL_SCANCODE_KP_5: return DIK_NUMPAD5;
        case SDL_SCANCODE_KP_6: return DIK_NUMPAD6;
        case SDL_SCANCODE_KP_PLUS: return DIK_ADD;
        case SDL_SCANCODE_KP_1: return DIK_NUMPAD1;
        case SDL_SCANCODE_KP_2: return DIK_NUMPAD2;
        case SDL_SCANCODE_KP_3: return DIK_NUMPAD3;
        case SDL_SCANCODE_KP_0: return DIK_NUMPAD0;
        case SDL_SCANCODE_KP_PERIOD: return DIK_DECIMAL;
        case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;
        case SDL_SCANCODE_KP_ENTER: return DIK_NUMPADENTER;
        case SDL_SCANCODE_RCTRL: return DIK_RCONTROL;
        case SDL_SCANCODE_KP_DIVIDE: return DIK_DIVIDE;
        case SDL_SCANCODE_PRINTSCREEN: return DIK_SYSRQ;
        case SDL_SCANCODE_RALT: return DIK_RMENU;
        case SDL_SCANCODE_PAUSE: return DIK_PAUSE;
        case SDL_SCANCODE_HOME: return DIK_HOME;
        case SDL_SCANCODE_UP: return DIK_UP;
        case SDL_SCANCODE_PAGEUP: return DIK_PRIOR;
        case SDL_SCANCODE_LEFT: return DIK_LEFT;
        case SDL_SCANCODE_RIGHT: return DIK_RIGHT;
        case SDL_SCANCODE_END: return DIK_END;
        case SDL_SCANCODE_DOWN: return DIK_DOWN;
        case SDL_SCANCODE_PAGEDOWN: return DIK_NEXT;
        case SDL_SCANCODE_INSERT: return DIK_INSERT;
        case SDL_SCANCODE_DELETE: return DIK_DELETE;
        case SDL_SCANCODE_LGUI: return DIK_LWIN;
        case SDL_SCANCODE_RGUI: return DIK_RWIN;
        case SDL_SCANCODE_APPLICATION: return DIK_APPS;
        default: return 0;
    }
}

const size_t kDefaultBufferSize = 64;

void AddPadMenuKeys(BYTE* pKeys);

class Device : public IDirectInputDevice2A
{
  public:
    explicit Device(bool gamepad) : mGamepad(gamepad)
    {
    }

    void Push(DWORD ofs, DWORD data)
    {
        if (!mAcquired)
        {
            return;
        }
        DIDEVICEOBJECTDATA d = {};
        d.dwOfs = ofs;
        d.dwData = data;
        d.dwTimeStamp = (DWORD)SDL_GetTicks();
        d.dwSequence = ++mSequence;
        if (mBuffer.size() >= mBufferSize)
        {
            mBuffer.pop_front();
            mOverflow = true;
        }
        mBuffer.push_back(d);
    }

    bool IsGamepad() const
    {
        return mGamepad;
    }

    // DIJOYSTATE as the game's data format lays it out: lX, lY at 0 and 4, rgdwPOV at 32, rgbButtons at 48.
    struct JoyState
    {
        LONG mAxes[8];
        DWORD mPov[4];
        BYTE mButtons[32];
    };
    JoyState mJoy = {{0}, {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}, {0}};
    BYTE mKeys[256] = {};

    STDMETHOD_(HRESULT, QueryInterface)(REFIID riid, void** ppv) override
    {
        if (IsEqualGUID(riid, IID_IDirectInputDeviceA) || IsEqualGUID(riid, IID_IDirectInputDevice2A) ||
            IsEqualGUID(riid, IID_IUnknown))
        {
            AddRef();
            *ppv = this;
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override
    {
        return ++mRefs;
    }
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(GetCapabilities)(LPDIDEVCAPS pCaps) override
    {
        pCaps->dwFlags = DIDC_ATTACHED;
        pCaps->dwDevType = mGamepad ? DIDEVTYPE_JOYSTICK : DIDEVTYPE_KEYBOARD;
        pCaps->dwAxes = mGamepad ? 2 : 0;
        pCaps->dwButtons = mGamepad ? 12 : 128;
        pCaps->dwPOVs = mGamepad ? 1 : 0;
        return DI_OK;
    }
    STDMETHOD(EnumObjects)(LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD) override
    {
        return DI_OK;
    }
    STDMETHOD(GetProperty)(REFGUID prop, LPDIPROPHEADER pHeader) override
    {
        if (&prop == &DIPROP_BUFFERSIZE)
        {
            reinterpret_cast<DIPROPDWORD*>(pHeader)->dwData = (DWORD)mBufferSize;
        }
        return DI_OK;
    }
    STDMETHOD(SetProperty)(REFGUID prop, LPCDIPROPHEADER pHeader) override
    {
        if (&prop == &DIPROP_BUFFERSIZE)
        {
            mBufferSize = reinterpret_cast<const DIPROPDWORD*>(pHeader)->dwData;
        }
        else if (&prop == &DIPROP_RANGE && pHeader->dwHow == DIPH_BYOFFSET && pHeader->dwObj < 8 * sizeof(LONG))
        {
            const DIPROPRANGE* pRange = reinterpret_cast<const DIPROPRANGE*>(pHeader);
            mRangeMin = pRange->lMin;
            mRangeMax = pRange->lMax;
        }
        else if (&prop == &DIPROP_DEADZONE)
        {
            mDeadZone = reinterpret_cast<const DIPROPDWORD*>(pHeader)->dwData;
        }
        return DI_OK;
    }
    STDMETHOD(Acquire)() override
    {
        mAcquired = true;
        return DI_OK;
    }
    STDMETHOD(Unacquire)() override
    {
        mAcquired = false;
        mBuffer.clear();
        return DI_OK;
    }
    STDMETHOD(GetDeviceState)(DWORD size, LPVOID pData) override
    {
        if (!mAcquired)
        {
            return DIERR_NOTACQUIRED;
        }
        memset(pData, 0, size);
        if (mGamepad)
        {
            memcpy(pData, &mJoy, size < sizeof(mJoy) ? size : sizeof(mJoy));
        }
        else if (size >= 256)
        {
            memcpy(pData, mKeys, 256);
            AddPadMenuKeys(static_cast<BYTE*>(pData));
        }
        return DI_OK;
    }
    STDMETHOD(GetDeviceData)(DWORD cbObject, LPDIDEVICEOBJECTDATA pData, LPDWORD pInOut, DWORD flags) override
    {
        if (!mAcquired)
        {
            *pInOut = 0;
            return DIERR_NOTACQUIRED;
        }
        const HRESULT result = mOverflow ? DI_BUFFEROVERFLOW : DI_OK;
        DWORD n = 0;
        const DWORD wanted = *pInOut;
        BYTE* pOut = reinterpret_cast<BYTE*>(pData);
        while (n < wanted && n < mBuffer.size())
        {
            if (pOut)
            {
                memset(pOut + n * cbObject, 0, cbObject);
                memcpy(pOut + n * cbObject, &mBuffer[n], cbObject < 16 ? cbObject : 16);
            }
            n++;
        }
        if (!(flags & DIGDD_PEEK))
        {
            mBuffer.erase(mBuffer.begin(), mBuffer.begin() + n);
            mOverflow = false;
        }
        *pInOut = n;
        return result;
    }
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT) override
    {
        return DI_OK;
    }
    STDMETHOD(SetEventNotification)(HANDLE) override
    {
        return DI_OK;
    }
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD) override
    {
        return DI_OK;
    }
    STDMETHOD(GetObjectInfo)(LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD) override
    {
        return DIERR_OBJECTNOTFOUND;
    }
    STDMETHOD(GetDeviceInfo)(LPDIDEVICEINSTANCEA pInfo) override
    {
        const DWORD size = pInfo->dwSize;
        memset(pInfo, 0, size);
        pInfo->dwSize = size;
        pInfo->guidInstance = mGamepad ? kGamepadGuid : GUID_SysKeyboard;
        pInfo->guidProduct = pInfo->guidInstance;
        pInfo->dwDevType = mGamepad ? DIDEVTYPE_JOYSTICK : DIDEVTYPE_KEYBOARD;
        strcpy(pInfo->tszInstanceName, mGamepad ? "Gamepad" : "Keyboard");
        strcpy(pInfo->tszProductName, pInfo->tszInstanceName);
        return DI_OK;
    }
    STDMETHOD(RunControlPanel)(HWND, DWORD) override
    {
        return DI_OK;
    }
    STDMETHOD(Initialize)(HINSTANCE, DWORD, REFGUID) override
    {
        return DI_OK;
    }
    STDMETHOD(CreateEffect)(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT*, LPUNKNOWN) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(EnumEffects)(LPDIENUMEFFECTSCALLBACKA, LPVOID, DWORD) override
    {
        return DI_OK;
    }
    STDMETHOD(GetEffectInfo)(LPDIEFFECTINFOA, REFGUID) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(GetForceFeedbackState)(LPDWORD) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(SendForceFeedbackCommand)(DWORD) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(EnumCreatedEffectObjects)(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override
    {
        return DI_OK;
    }
    STDMETHOD(Escape)(LPDIEFFESCAPE) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(Poll)() override
    {
        return DI_OK;
    }
    STDMETHOD(SendDeviceData)(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override
    {
        return DIERR_UNSUPPORTED;
    }

    LONG ScaleAxis(Sint16 v) const
    {
        const LONG dead = (LONG)(32767LL * mDeadZone / 10000);
        LONG a = v < -32767 ? -32767 : v;
        if (a > -dead && a < dead)
        {
            a = 0;
        }
        const LONG mid = (mRangeMin + mRangeMax) / 2;
        const LONG half = (mRangeMax - mRangeMin) / 2;
        return mid + (LONG)((long long)a * half / 32767);
    }

    LONG mRangeMin = 0;
    LONG mRangeMax = 65535;
    DWORD mDeadZone = 0;

  private:
    bool mGamepad;
    ULONG mRefs = 1;
    bool mAcquired = false;
    bool mOverflow = false;
    DWORD mSequence = 0;
    size_t mBufferSize = kDefaultBufferSize;
    std::deque<DIDEVICEOBJECTDATA> mBuffer;
};

std::vector<Device*> sDevices;
SDL_Gamepad* sGamepad;
bool sPadMode; // the last input came from the gamepad
Sint16 sStick[2];
bool sDpad[4]; // up, down, left, right

ULONG STDMETHODCALLTYPE Device::Release()
{
    if (--mRefs)
    {
        return mRefs;
    }
    sDevices.erase(std::remove(sDevices.begin(), sDevices.end(), this), sDevices.end());
    delete this;
    return 0;
}

// The first gamepad, or the first whose name contains $GTA2_GAMEPAD.
void OpenGamepad()
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

// The stick and the d-pad drive the same two axes, as a digital pad of the time did.
void UpdateAxes()
{
    for (Device* pDev : sDevices)
    {
        if (!pDev->IsGamepad())
        {
            continue;
        }
        Sint16 x = sStick[0];
        Sint16 y = sStick[1];
        if (sDpad[2] != sDpad[3])
        {
            x = sDpad[2] ? -32767 : 32767;
        }
        if (sDpad[0] != sDpad[1])
        {
            y = sDpad[0] ? -32767 : 32767;
        }
        const LONG ax = pDev->ScaleAxis(x);
        const LONG ay = pDev->ScaleAxis(y);
        if (ax != pDev->mJoy.mAxes[0])
        {
            pDev->mJoy.mAxes[0] = ax;
            pDev->Push(DIJOFS_X, (DWORD)ax);
        }
        if (ay != pDev->mJoy.mAxes[1])
        {
            pDev->mJoy.mAxes[1] = ay;
            pDev->Push(DIJOFS_Y, (DWORD)ay);
        }
    }
}

int ButtonIndex(Uint8 button)
{
    switch (button)
    {
        case SDL_GAMEPAD_BUTTON_SOUTH: return 0;
        case SDL_GAMEPAD_BUTTON_EAST: return 1;
        case SDL_GAMEPAD_BUTTON_WEST: return 2;
        case SDL_GAMEPAD_BUTTON_NORTH: return 3;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 4;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 5;
        case SDL_GAMEPAD_BUTTON_BACK: return 6;
        case SDL_GAMEPAD_BUTTON_START: return 7;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return 8;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return 9;
        default: return -1;
    }
}

void SetButton(int index, bool down)
{
    for (Device* pDev : sDevices)
    {
        if (pDev->IsGamepad() && (pDev->mJoy.mButtons[index] != 0) != down)
        {
            pDev->mJoy.mButtons[index] = down ? 0x80 : 0;
            pDev->Push(DIJOFS_BUTTON(index), down ? 0x80 : 0);
        }
    }
}

class DirectInput : public IDirectInputA
{
  public:
    STDMETHOD_(HRESULT, QueryInterface)(REFIID, void** ppv) override
    {
        AddRef();
        *ppv = this;
        return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() override
    {
        return ++mRefs;
    }
    STDMETHOD_(ULONG, Release)() override
    {
        return --mRefs;
    }
    STDMETHOD(CreateDevice)(REFGUID guid, LPDIRECTINPUTDEVICEA* ppDevice, LPUNKNOWN) override
    {
        Device* pDev;
        if (IsEqualGUID(guid, GUID_SysKeyboard))
        {
            pDev = new Device(false);
        }
        else if (IsEqualGUID(guid, kGamepadGuid) && sGamepad)
        {
            pDev = new Device(true);
        }
        else
        {
            *ppDevice = NULL;
            return DIERR_DEVICENOTREG;
        }
        sDevices.push_back(pDev);
        *ppDevice = pDev;
        return DI_OK;
    }
    STDMETHOD(EnumDevices)(DWORD type, LPDIENUMDEVICESCALLBACKA pCallback, LPVOID pRef, DWORD) override
    {
        OpenGamepad();
        if (sGamepad && (type == 0 || GET_DIDEVICE_TYPE(type) == DIDEVTYPE_JOYSTICK))
        {
            DIDEVICEINSTANCEA inst = {};
            inst.dwSize = sizeof(inst);
            inst.guidInstance = kGamepadGuid;
            inst.guidProduct = kGamepadGuid;
            inst.dwDevType = DIDEVTYPE_JOYSTICK;
            SDL_strlcpy(inst.tszInstanceName, SDL_GetGamepadName(sGamepad), sizeof(inst.tszInstanceName));
            SDL_strlcpy(inst.tszProductName, inst.tszInstanceName, sizeof(inst.tszProductName));
            pCallback(&inst, pRef);
        }
        return DI_OK;
    }
    STDMETHOD(GetDeviceStatus)(REFGUID) override
    {
        return DI_OK;
    }
    STDMETHOD(RunControlPanel)(HWND, DWORD) override
    {
        return DI_OK;
    }
    STDMETHOD(Initialize)(HINSTANCE, DWORD) override
    {
        return DI_OK;
    }

  private:
    ULONG mRefs = 1;
};

DirectInput sDirectInput;
} // namespace

namespace
{
// Only the frontend reads the keyboard's state (the game reads its buffered events): the gamepad
// drives the menus as the arrows, Enter (A, Start) and Escape (B, Back).
void AddPadMenuKeys(BYTE* pKeys)
{
    if (!sGamepad)
    {
        return;
    }
    const Sint16 x = SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_LEFTX);
    const Sint16 y = SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_LEFTY);
    const Sint16 kStick = 16000;
    const struct
    {
        bool mDown;
        DWORD mDik;
    } keys[] = {
        {SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -kStick, DIK_UP},
        {SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > kStick, DIK_DOWN},
        {SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -kStick, DIK_LEFT},
        {SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > kStick, DIK_RIGHT},
        {SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_START), DIK_RETURN},
        {SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_EAST) || SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_BACK), DIK_ESCAPE},
    };
    for (const auto& k : keys)
    {
        if (k.mDown)
        {
            pKeys[k.mDik] = 0x80;
        }
    }
}
} // namespace

bool port_PadMode()
{
    return sPadMode;
}

bool port_PadInput(BYTE* pButtons, LONG* pX, LONG* pY)
{
    if (!sGamepad)
    {
        return false;
    }
    for (int b = 0; b < 32; b++)
    {
        const int index = b;
        pButtons[b] = 0;
        for (int sdl = 0; sdl < SDL_GAMEPAD_BUTTON_COUNT; sdl++)
        {
            if (ButtonIndex((Uint8)sdl) == index && SDL_GetGamepadButton(sGamepad, (SDL_GamepadButton)sdl))
            {
                pButtons[b] = 0x80;
            }
        }
    }
    if (SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16384)
    {
        pButtons[10] = 0x80;
    }
    if (SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384)
    {
        pButtons[11] = 0x80;
    }
    Sint16 x = SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_LEFTX);
    Sint16 y = SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_LEFTY);
    if (SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) != SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
    {
        x = SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -32767 : 32767;
    }
    if (SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) != SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))
    {
        y = SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ? -32767 : 32767;
    }
    *pX = x * 1000 / 32767;
    *pY = y * 1000 / 32767;
    return true;
}

void port_OnInputEvent(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
    {
        sPadMode = false;
    }
    else if (sGamepad && ((e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.which == SDL_GetGamepadID(sGamepad)) ||
                          (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.which == SDL_GetGamepadID(sGamepad) &&
                           (e.gaxis.value > 16000 || e.gaxis.value < -16000))))
    {
        sPadMode = true;
    }
    switch (e.type)
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const DWORD dik = Dik(e.key.scancode);
            const bool down = e.type == SDL_EVENT_KEY_DOWN;
            if (!dik || e.key.repeat)
            {
                break;
            }
            for (Device* pDev : sDevices)
            {
                if (!pDev->IsGamepad() && (pDev->mKeys[dik] != 0) != down)
                {
                    pDev->mKeys[dik] = down ? 0x80 : 0;
                    pDev->Push(dik, down ? 0x80 : 0);
                }
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
            OpenGamepad();
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (sGamepad && SDL_GetGamepadID(sGamepad) == e.gdevice.which)
            {
                SDL_CloseGamepad(sGamepad);
                sGamepad = NULL;
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (!sGamepad || e.gaxis.which != SDL_GetGamepadID(sGamepad))
            {
                break;
            }
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
            {
                sStick[e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY] = e.gaxis.value;
                UpdateAxes();
            }
            else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            {
                SetButton(e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 10 : 11, e.gaxis.value > 16384);
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        {
            if (!sGamepad || e.gbutton.which != SDL_GetGamepadID(sGamepad))
            {
                break;
            }
            const bool down = e.gbutton.down;
            switch (e.gbutton.button)
            {
                case SDL_GAMEPAD_BUTTON_DPAD_UP: sDpad[0] = down; UpdateAxes(); break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: sDpad[1] = down; UpdateAxes(); break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: sDpad[2] = down; UpdateAxes(); break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: sDpad[3] = down; UpdateAxes(); break;
                default:
                {
                    const int index = ButtonIndex(e.gbutton.button);
                    if (index >= 0)
                    {
                        SetButton(index, down);
                    }
                    break;
                }
            }
            break;
        }
        default:
            break;
    }
}

extern "C" HRESULT WINAPI DirectInputCreateA(HINSTANCE, DWORD, LPDIRECTINPUTA* ppDI, LPUNKNOWN)
{
    OpenGamepad();
    *ppDI = &sDirectInput;
    return DI_OK;
}
