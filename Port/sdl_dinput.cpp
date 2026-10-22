// Port (docs/port.md): the DirectInput the game uses, on SDL3 events. Port_DirectInputCreateA hands out an
// IDirectInputA whose devices answer the calls of InputRecorder_67F8B0.cpp and winmain.cpp, so the game's
// input code runs unchanged: the keyboard returns SDL key events as buffered DIK events, as DirectInput did.
// ponytail: no gamepad (EnumDevices finds none) and no mouse data yet; add an SDL_Gamepad-backed device
// for the joystick data format (gInputDeviceFormat_601A6C) when gamepads are wanted.
#define DIRECTINPUT_VERSION 0x0700
#include <SDL3/SDL.h>
#include <deque>
#include <dinput.h>

void Port_DInputOnEvent(const SDL_Event& e);

namespace
{
struct KeyEvent
{
    DWORD dik;
    DWORD data;
    DWORD time;
};
std::deque<KeyEvent> gKeyEvents; // buffered events (GetDeviceData, the game)
unsigned char gKeyState[256]; // immediate state (GetDeviceState, the frontend)
DWORD gSequence;

void AddKeyEvent(DWORD dik, bool down, DWORD time)
{
    KeyEvent ev = {dik, down ? 0x80u : 0u, time};
    if (gKeyEvents.size() < 64) // DirectInput drops events beyond its buffer (the game sets a small DIPROP_BUFFERSIZE)
    {
        gKeyEvents.push_back(ev);
    }
    gKeyState[dik & 0xFF] = down ? 0x80 : 0;
}

// Test hook: GTA2_PORT_KEYS=<ms>:<dik>:<1 down|0 up>,... queues key events at those times after start.
void QueueScriptedKeys()
{
    static const char* pNext = SDL_getenv("GTA2_PORT_KEYS");
    while (pNext && *pNext)
    {
        unsigned ms = 0, dik = 0, down = 0;
        if (SDL_sscanf(pNext, "%u:%u:%u", &ms, &dik, &down) != 3 || ms > SDL_GetTicks())
        {
            return;
        }
        AddKeyEvent(dik, down != 0, ms);
        pNext = SDL_strchr(pNext, ',');
        pNext = pNext ? pNext + 1 : NULL;
    }
}

// SDL scancodes (USB HID) to DirectInput key codes (PC set 1 scancodes).
DWORD ToDik(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
    {
        static const unsigned char kLetters[26] = {DIK_A, DIK_B, DIK_C, DIK_D, DIK_E, DIK_F, DIK_G, DIK_H, DIK_I,
                                                   DIK_J, DIK_K, DIK_L, DIK_M, DIK_N, DIK_O, DIK_P, DIK_Q, DIK_R,
                                                   DIK_S, DIK_T, DIK_U, DIK_V, DIK_W, DIK_X, DIK_Y, DIK_Z};
        return kLetters[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0)
    {
        return DIK_1 + (sc - SDL_SCANCODE_1); // DIK_1..DIK_0 are consecutive like SDL's 1..0
    }
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10)
    {
        return DIK_F1 + (sc - SDL_SCANCODE_F1);
    }
    switch (sc)
    {
        case SDL_SCANCODE_RETURN: return DIK_RETURN;
        case SDL_SCANCODE_ESCAPE: return DIK_ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return DIK_BACK;
        case SDL_SCANCODE_TAB: return DIK_TAB;
        case SDL_SCANCODE_SPACE: return DIK_SPACE;
        case SDL_SCANCODE_MINUS: return DIK_MINUS;
        case SDL_SCANCODE_EQUALS: return DIK_EQUALS;
        case SDL_SCANCODE_LEFTBRACKET: return DIK_LBRACKET;
        case SDL_SCANCODE_RIGHTBRACKET: return DIK_RBRACKET;
        case SDL_SCANCODE_BACKSLASH: return DIK_BACKSLASH;
        case SDL_SCANCODE_SEMICOLON: return DIK_SEMICOLON;
        case SDL_SCANCODE_APOSTROPHE: return DIK_APOSTROPHE;
        case SDL_SCANCODE_GRAVE: return DIK_GRAVE;
        case SDL_SCANCODE_COMMA: return DIK_COMMA;
        case SDL_SCANCODE_PERIOD: return DIK_PERIOD;
        case SDL_SCANCODE_SLASH: return DIK_SLASH;
        case SDL_SCANCODE_CAPSLOCK: return DIK_CAPITAL;
        case SDL_SCANCODE_F11: return DIK_F11;
        case SDL_SCANCODE_F12: return DIK_F12;
        case SDL_SCANCODE_PRINTSCREEN: return DIK_SYSRQ;
        case SDL_SCANCODE_SCROLLLOCK: return DIK_SCROLL;
        case SDL_SCANCODE_PAUSE: return DIK_PAUSE;
        case SDL_SCANCODE_INSERT: return DIK_INSERT;
        case SDL_SCANCODE_HOME: return DIK_HOME;
        case SDL_SCANCODE_PAGEUP: return DIK_PRIOR;
        case SDL_SCANCODE_DELETE: return DIK_DELETE;
        case SDL_SCANCODE_END: return DIK_END;
        case SDL_SCANCODE_PAGEDOWN: return DIK_NEXT;
        case SDL_SCANCODE_RIGHT: return DIK_RIGHT;
        case SDL_SCANCODE_LEFT: return DIK_LEFT;
        case SDL_SCANCODE_DOWN: return DIK_DOWN;
        case SDL_SCANCODE_UP: return DIK_UP;
        case SDL_SCANCODE_NUMLOCKCLEAR: return DIK_NUMLOCK;
        case SDL_SCANCODE_KP_DIVIDE: return DIK_DIVIDE;
        case SDL_SCANCODE_KP_MULTIPLY: return DIK_MULTIPLY;
        case SDL_SCANCODE_KP_MINUS: return DIK_SUBTRACT;
        case SDL_SCANCODE_KP_PLUS: return DIK_ADD;
        case SDL_SCANCODE_KP_ENTER: return DIK_NUMPADENTER;
        case SDL_SCANCODE_KP_1: return DIK_NUMPAD1;
        case SDL_SCANCODE_KP_2: return DIK_NUMPAD2;
        case SDL_SCANCODE_KP_3: return DIK_NUMPAD3;
        case SDL_SCANCODE_KP_4: return DIK_NUMPAD4;
        case SDL_SCANCODE_KP_5: return DIK_NUMPAD5;
        case SDL_SCANCODE_KP_6: return DIK_NUMPAD6;
        case SDL_SCANCODE_KP_7: return DIK_NUMPAD7;
        case SDL_SCANCODE_KP_8: return DIK_NUMPAD8;
        case SDL_SCANCODE_KP_9: return DIK_NUMPAD9;
        case SDL_SCANCODE_KP_0: return DIK_NUMPAD0;
        case SDL_SCANCODE_KP_PERIOD: return DIK_DECIMAL;
        case SDL_SCANCODE_LCTRL: return DIK_LCONTROL;
        case SDL_SCANCODE_LSHIFT: return DIK_LSHIFT;
        case SDL_SCANCODE_LALT: return DIK_LMENU;
        case SDL_SCANCODE_LGUI: return DIK_LWIN;
        case SDL_SCANCODE_RCTRL: return DIK_RCONTROL;
        case SDL_SCANCODE_RSHIFT: return DIK_RSHIFT;
        case SDL_SCANCODE_RALT: return DIK_RMENU;
        case SDL_SCANCODE_RGUI: return DIK_RWIN;
        default: return 0;
    }
}

// One device class for the keyboard and the mouse; only the keyboard has data.
class Device : public IDirectInputDevice2A
{
  public:
    explicit Device(bool keyboard) : mRefs(1), mKeyboard(keyboard)
    {
    }

    STDMETHOD(QueryInterface)(REFIID, LPVOID* ppv)
    {
        AddRef();
        *ppv = this; // the game asks for IDirectInputDevice2A, which this is
        return DI_OK;
    }
    STDMETHOD_(ULONG, AddRef)()
    {
        return ++mRefs;
    }
    STDMETHOD_(ULONG, Release)()
    {
        const ULONG refs = --mRefs;
        if (!refs)
        {
            delete this;
        }
        return refs;
    }
    STDMETHOD(GetCapabilities)(LPDIDEVCAPS)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(EnumObjects)(LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD)
    {
        return DI_OK;
    }
    STDMETHOD(GetProperty)(REFGUID, LPDIPROPHEADER)
    {
        return DI_OK;
    }
    STDMETHOD(SetProperty)(REFGUID, LPCDIPROPHEADER)
    {
        return DI_OK;
    }
    STDMETHOD(Acquire)()
    {
        return DI_OK;
    }
    STDMETHOD(Unacquire)()
    {
        return DI_OK;
    }
    STDMETHOD(GetDeviceState)(DWORD cb, LPVOID pData)
    {
        memset(pData, 0, cb);
        if (mKeyboard)
        {
            QueueScriptedKeys();
            memcpy(pData, gKeyState, cb < sizeof(gKeyState) ? cb : sizeof(gKeyState));
        }
        return DI_OK;
    }
    // Buffered events: rgdod NULL with *pdwInOut = INFINITE flushes; DIGDD_PEEK leaves them queued.
    STDMETHOD(GetDeviceData)(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD flags)
    {
        if (!mKeyboard)
        {
            *pdwInOut = 0;
            return DI_OK;
        }
        QueueScriptedKeys();
        DWORD n = gKeyEvents.size() < *pdwInOut ? (DWORD)gKeyEvents.size() : *pdwInOut;
        for (DWORD i = 0; rgdod && i < n; i++)
        {
            DWORD* pOut = (DWORD*)((char*)rgdod + i * cbObjectData); // dwOfs, dwData, dwTimeStamp, dwSequence
            pOut[0] = gKeyEvents[i].dik;
            pOut[1] = gKeyEvents[i].data;
            pOut[2] = gKeyEvents[i].time;
            pOut[3] = gSequence++;
        }
        if (!(flags & DIGDD_PEEK))
        {
            gKeyEvents.erase(gKeyEvents.begin(), gKeyEvents.begin() + n);
        }
        *pdwInOut = n;
        return DI_OK;
    }
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT)
    {
        return DI_OK;
    }
    STDMETHOD(SetEventNotification)(HANDLE)
    {
        return DI_OK;
    }
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD)
    {
        return DI_OK;
    }
    STDMETHOD(GetObjectInfo)(LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(GetDeviceInfo)(LPDIDEVICEINSTANCEA)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(RunControlPanel)(HWND, DWORD)
    {
        return DI_OK;
    }
    STDMETHOD(Initialize)(HINSTANCE, DWORD, REFGUID)
    {
        return DI_OK;
    }
    STDMETHOD(CreateEffect)(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT*, LPUNKNOWN)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(EnumEffects)(LPDIENUMEFFECTSCALLBACKA, LPVOID, DWORD)
    {
        return DI_OK;
    }
    STDMETHOD(GetEffectInfo)(LPDIEFFECTINFOA, REFGUID)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(GetForceFeedbackState)(LPDWORD)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(SendForceFeedbackCommand)(DWORD)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(EnumCreatedEffectObjects)(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD)
    {
        return DI_OK;
    }
    STDMETHOD(Escape)(LPDIEFFESCAPE)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(Poll)()
    {
        return DI_OK;
    }
    STDMETHOD(SendDeviceData)(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD)
    {
        return DIERR_UNSUPPORTED;
    }

  private:
    ULONG mRefs;
    bool mKeyboard;
};

class DInput : public IDirectInputA
{
  public:
    STDMETHOD(QueryInterface)(REFIID, LPVOID* ppv)
    {
        *ppv = this;
        return DI_OK;
    }
    STDMETHOD_(ULONG, AddRef)()
    {
        return 1;
    }
    STDMETHOD_(ULONG, Release)()
    {
        return 0; // one static instance
    }
    STDMETHOD(CreateDevice)(REFGUID guid, LPDIRECTINPUTDEVICEA* ppDevice, LPUNKNOWN)
    {
        *ppDevice = new Device(IsEqualGUID(guid, GUID_SysKeyboard));
        return DI_OK;
    }
    STDMETHOD(EnumDevices)(DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD)
    {
        return DI_OK; // no gamepads yet
    }
    STDMETHOD(GetDeviceStatus)(REFGUID)
    {
        return DI_OK;
    }
    STDMETHOD(RunControlPanel)(HWND, DWORD)
    {
        return DI_OK;
    }
    STDMETHOD(Initialize)(HINSTANCE, DWORD)
    {
        return DI_OK;
    }
};
DInput gDInput;
} // namespace

// Called with every SDL event the window loop sees (sdl_win32.cpp).
void Port_DInputOnEvent(const SDL_Event& e)
{
    if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && !e.key.repeat)
    {
        const DWORD dik = ToDik(e.key.scancode);
        if (dik)
        {
            AddKeyEvent(dik, e.type == SDL_EVENT_KEY_DOWN, (DWORD)SDL_GetTicks());
        }
    }
}

HRESULT WINAPI Port_DirectInputCreateA(HINSTANCE, DWORD, LPDIRECTINPUTA* ppDI, LPUNKNOWN)
{
    *ppDI = &gDInput;
    return DI_OK;
}
