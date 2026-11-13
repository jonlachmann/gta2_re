// DirectInput on SDL3: the keyboard as a DirectInput device with DIK_* key codes (pad.cpp turns the
// gamepad into keys). Only real state changes are buffered (no key repeat, no lock key resyncs):
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

class Device : public IDirectInputDevice2A
{
  public:

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
        pCaps->dwDevType = DIDEVTYPE_KEYBOARD;
        pCaps->dwButtons = 128;
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
        if (size >= 256)
        {
            memcpy(pData, mKeys, 256);
            port_AddPadMenuKeys(static_cast<BYTE*>(pData));
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
        pInfo->guidInstance = GUID_SysKeyboard;
        pInfo->guidProduct = pInfo->guidInstance;
        pInfo->dwDevType = DIDEVTYPE_KEYBOARD;
        strcpy(pInfo->tszInstanceName, "Keyboard");
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

  private:
    ULONG mRefs = 1;
    bool mAcquired = false;
    bool mOverflow = false;
    DWORD mSequence = 0;
    size_t mBufferSize = kDefaultBufferSize;
    std::deque<DIDEVICEOBJECTDATA> mBuffer;
};

std::vector<Device*> sDevices;

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
            pDev = new Device();
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
    // No joystick: pad.cpp gives the gamepad to the game as keys.
    STDMETHOD(EnumDevices)(DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD) override
    {
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
unsigned short sTypedChar[256]; // the character each key last typed, in the keyboard's layout with its shift (port_TypedChar)
} // namespace

unsigned short port_TypedChar(int dik)
{
    return sTypedChar[dik & 0xFF];
}

DWORD port_DikFromScancode(SDL_Scancode sc)
{
    return Dik(sc);
}

int port_KeyLetter(int dik)
{
    for (int sc = 0; sc < SDL_SCANCODE_COUNT; sc++)
    {
        if (dik && Dik(static_cast<SDL_Scancode>(sc)) == static_cast<DWORD>(dik))
        {
            const SDL_Keycode key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
            return key >= 'a' && key <= 'z' ? static_cast<int>(key - 'a' + 'A') : 0;
        }
    }
    return 0;
}

void port_PushKey(DWORD dik, bool down)
{
    for (Device* pDev : sDevices)
    {
        pDev->Push(dik, down ? 0x80 : 0);
    }
}

void port_OnInputEvent(const SDL_Event& e)
{
    port_PadEvent(e);
    port_MouseEvent(e);
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        // The releases of the keys held now would go to the next window.
        for (Device* pDev : sDevices)
        {
            for (DWORD dik = 0; dik < 256; dik++)
            {
                if (pDev->mKeys[dik])
                {
                    pDev->mKeys[dik] = 0;
                    pDev->Push(dik, 0);
                }
            }
        }
        return;
    }
    if ((e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP) || e.key.repeat)
    {
        return;
    }
    const DWORD dik = Dik(e.key.scancode);
    const bool down = e.type == SDL_EVENT_KEY_DOWN;
    if (!dik)
    {
        return;
    }
    if (down)
    {
        // Lower case as typed, where the game's tables give capitals: the chat's font has it, and its commands are
        // case-insensitive
        const SDL_Keycode c = SDL_GetKeyFromScancode(e.key.scancode, e.key.mod, false);
        sTypedChar[dik & 0xFF] = c > ' ' && c < 256 && c != SDLK_DELETE ? static_cast<unsigned short>(c) : 0;
    }
    const bool buffered = !port_AltKeyInGame(dik);
    for (Device* pDev : sDevices)
    {
        if ((pDev->mKeys[dik] != 0) != down)
        {
            pDev->mKeys[dik] = down ? 0x80 : 0;
            if (buffered)
            {
                pDev->Push(dik, down ? 0x80 : 0);
            }
        }
    }
}

extern "C" HRESULT WINAPI DirectInputCreateA(HINSTANCE, DWORD, LPDIRECTINPUTA* ppDI, LPUNKNOWN)
{
    port_OpenGamepad();
    *ppDI = &sDirectInput;
    return DI_OK;
}
