// The DirectInput 7 subset the game uses; port/compat/src/dinput.cpp implements it on SDL3.
#pragma once

#include "windows.h"

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0700
#endif

DEFINE_GUID(IID_IDirectInputA, 0x89521360, 0xAA8A, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(IID_IDirectInputDeviceA, 0x5944E680, 0xC92E, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(IID_IDirectInputDevice2A, 0x5944E682, 0xC92E, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_XAxis, 0xA36D02E0, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_YAxis, 0xA36D02E1, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_ZAxis, 0xA36D02E2, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_RxAxis, 0xA36D02F4, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_RyAxis, 0xA36D02F5, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_RzAxis, 0xA36D02E3, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_Slider, 0xA36D02E4, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_Key, 0x55728220, 0xD33C, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_POV, 0xA36D02F2, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_SysMouse, 0x6F1D2B60, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_GUID(GUID_SysKeyboard, 0x6F1D2B61, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

#define DI_OK S_OK
#define DI_BUFFEROVERFLOW S_FALSE
#define DIERR_OBJECTNOTFOUND MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_FILE_NOT_FOUND)
#define DIERR_NOTACQUIRED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_INVALID_ACCESS)
#define DIERR_INPUTLOST MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_READ_FAULT)
#define DIERR_UNSUPPORTED E_NOTIMPL
#define DIERR_DEVICENOTREG REGDB_E_CLASSNOTREG

#define DIDEVTYPE_KEYBOARD 3
#define DIDEVTYPE_JOYSTICK 4
#define GET_DIDEVICE_TYPE(t) LOBYTE(t)
#define DIDC_ATTACHED 0x00000001
#define DIDF_ABSAXIS 0x00000001
#define DIDF_RELAXIS 0x00000002
#define DIEDFL_ATTACHEDONLY 0x00000001
#define DIENUM_STOP 0
#define DIENUM_CONTINUE 1
#define DIGDD_PEEK 0x00000001
#define DIPH_DEVICE 0
#define DIPH_BYOFFSET 1
#define DISCL_EXCLUSIVE 0x00000001
#define DISCL_NONEXCLUSIVE 0x00000002
#define DISCL_FOREGROUND 0x00000004
#define DISCL_BACKGROUND 0x00000008

// Properties are named by small integers passed as GUID references.
#define MAKEDIPROP(prop) (*(const GUID*)(prop))
#define DIPROP_BUFFERSIZE MAKEDIPROP(1)
#define DIPROP_RANGE MAKEDIPROP(4)
#define DIPROP_DEADZONE MAKEDIPROP(5)
#define DIPROP_SATURATION MAKEDIPROP(6)

typedef struct DIDEVCAPS
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwDevType;
    DWORD dwAxes;
    DWORD dwButtons;
    DWORD dwPOVs;
    DWORD dwFFSamplePeriod;
    DWORD dwFFMinTimeResolution;
    DWORD dwFirmwareRevision;
    DWORD dwHardwareRevision;
    DWORD dwFFDriverVersion;
} DIDEVCAPS, *LPDIDEVCAPS;

typedef struct _DIOBJECTDATAFORMAT
{
    const GUID* pguid;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
} DIOBJECTDATAFORMAT, *LPDIOBJECTDATAFORMAT;

typedef struct _DIDATAFORMAT
{
    DWORD dwSize;
    DWORD dwObjSize;
    DWORD dwFlags;
    DWORD dwDataSize;
    DWORD dwNumObjs;
    LPDIOBJECTDATAFORMAT rgodf;
} DIDATAFORMAT, *LPDIDATAFORMAT;
typedef const DIDATAFORMAT* LPCDIDATAFORMAT;

// DirectX 7 layout (no uAppData): the game passes 16 as the record size.
typedef struct DIDEVICEOBJECTDATA
{
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;
typedef const DIDEVICEOBJECTDATA* LPCDIDEVICEOBJECTDATA;

typedef struct DIPROPHEADER
{
    DWORD dwSize;
    DWORD dwHeaderSize;
    DWORD dwObj;
    DWORD dwHow;
} DIPROPHEADER, *LPDIPROPHEADER;
typedef const DIPROPHEADER* LPCDIPROPHEADER;

typedef struct DIPROPDWORD
{
    DIPROPHEADER diph;
    DWORD dwData;
} DIPROPDWORD;

typedef struct DIPROPRANGE
{
    DIPROPHEADER diph;
    LONG lMin;
    LONG lMax;
} DIPROPRANGE;

typedef struct DIDEVICEINSTANCEA
{
    DWORD dwSize;
    GUID guidInstance;
    GUID guidProduct;
    DWORD dwDevType;
    CHAR tszInstanceName[MAX_PATH];
    CHAR tszProductName[MAX_PATH];
    GUID guidFFDriver;
    WORD wUsagePage;
    WORD wUsage;
} DIDEVICEINSTANCEA, *LPDIDEVICEINSTANCEA;
typedef const DIDEVICEINSTANCEA* LPCDIDEVICEINSTANCEA;

typedef struct DIJOYSTATE
{
    LONG lX;
    LONG lY;
    LONG lZ;
    LONG lRx;
    LONG lRy;
    LONG lRz;
    LONG rglSlider[2];
    DWORD rgdwPOV[4];
    BYTE rgbButtons[32];
} DIJOYSTATE;
#define DIJOFS_X offsetof(DIJOYSTATE, lX)
#define DIJOFS_Y offsetof(DIJOYSTATE, lY)
#define DIJOFS_BUTTON(n) (offsetof(DIJOYSTATE, rgbButtons) + (n))

#if defined(__i386__) || defined(_M_IX86)
static_assert(sizeof(DIDATAFORMAT) == 24 && sizeof(DIOBJECTDATAFORMAT) == 16 && sizeof(DIDEVICEOBJECTDATA) == 16, "DirectInput layout");
static_assert(sizeof(DIPROPDWORD) == 20 && sizeof(DIPROPRANGE) == 24 && sizeof(DIDEVICEINSTANCEA) == 580, "DirectInput layout");
static_assert(sizeof(DIJOYSTATE) == 80 && sizeof(DIDEVCAPS) == 44, "DirectInput layout");
#endif

// Only passed through by the port.
typedef struct DIDEVICEOBJECTINSTANCEA* LPDIDEVICEOBJECTINSTANCEA;
typedef const struct DIEFFECT* LPCDIEFFECT;
typedef struct DIEFFECTINFOA* LPDIEFFECTINFOA;
typedef struct DIEFFESCAPE* LPDIEFFESCAPE;
typedef struct IDirectInputEffect* LPDIRECTINPUTEFFECT;

typedef BOOL(CALLBACK* LPDIENUMDEVICESCALLBACKA)(LPCDIDEVICEINSTANCEA, LPVOID);
typedef BOOL(CALLBACK* LPDIENUMDEVICEOBJECTSCALLBACKA)(const struct DIDEVICEOBJECTINSTANCEA*, LPVOID);
typedef BOOL(CALLBACK* LPDIENUMEFFECTSCALLBACKA)(const struct DIEFFECTINFOA*, LPVOID);
typedef BOOL(CALLBACK* LPDIENUMCREATEDEFFECTOBJECTSCALLBACK)(LPDIRECTINPUTEFFECT, LPVOID);

DECLARE_INTERFACE_(IDirectInputDeviceA, IUnknown)
{
    STDMETHOD(GetCapabilities)(LPDIDEVCAPS pCaps) PURE;
    STDMETHOD(EnumObjects)(LPDIENUMDEVICEOBJECTSCALLBACKA pCallback, LPVOID pRef, DWORD flags) PURE;
    STDMETHOD(GetProperty)(REFGUID prop, LPDIPROPHEADER pHeader) PURE;
    STDMETHOD(SetProperty)(REFGUID prop, LPCDIPROPHEADER pHeader) PURE;
    STDMETHOD(Acquire)() PURE;
    STDMETHOD(Unacquire)() PURE;
    STDMETHOD(GetDeviceState)(DWORD size, LPVOID pData) PURE;
    STDMETHOD(GetDeviceData)(DWORD cbObject, LPDIDEVICEOBJECTDATA pData, LPDWORD pInOut, DWORD flags) PURE;
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT pFormat) PURE;
    STDMETHOD(SetEventNotification)(HANDLE event) PURE;
    STDMETHOD(SetCooperativeLevel)(HWND h, DWORD flags) PURE;
    STDMETHOD(GetObjectInfo)(LPDIDEVICEOBJECTINSTANCEA pInfo, DWORD obj, DWORD how) PURE;
    STDMETHOD(GetDeviceInfo)(LPDIDEVICEINSTANCEA pInfo) PURE;
    STDMETHOD(RunControlPanel)(HWND owner, DWORD flags) PURE;
    STDMETHOD(Initialize)(HINSTANCE instance, DWORD version, REFGUID guid) PURE;
};

DECLARE_INTERFACE_(IDirectInputDevice2A, IDirectInputDeviceA)
{
    STDMETHOD(CreateEffect)(REFGUID guid, LPCDIEFFECT pEffect, LPDIRECTINPUTEFFECT* ppEffect, LPUNKNOWN outer) PURE;
    STDMETHOD(EnumEffects)(LPDIENUMEFFECTSCALLBACKA pCallback, LPVOID pRef, DWORD type) PURE;
    STDMETHOD(GetEffectInfo)(LPDIEFFECTINFOA pInfo, REFGUID guid) PURE;
    STDMETHOD(GetForceFeedbackState)(LPDWORD pOut) PURE;
    STDMETHOD(SendForceFeedbackCommand)(DWORD flags) PURE;
    STDMETHOD(EnumCreatedEffectObjects)(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK pCallback, LPVOID pRef, DWORD flags) PURE;
    STDMETHOD(Escape)(LPDIEFFESCAPE pEscape) PURE;
    STDMETHOD(Poll)() PURE;
    STDMETHOD(SendDeviceData)(DWORD cbObject, LPCDIDEVICEOBJECTDATA pData, LPDWORD pInOut, DWORD flags) PURE;
};

DECLARE_INTERFACE_(IDirectInputA, IUnknown)
{
    STDMETHOD(CreateDevice)(REFGUID guid, IDirectInputDeviceA** ppDevice, LPUNKNOWN outer) PURE;
    STDMETHOD(EnumDevices)(DWORD type, LPDIENUMDEVICESCALLBACKA pCallback, LPVOID pRef, DWORD flags) PURE;
    STDMETHOD(GetDeviceStatus)(REFGUID guid) PURE;
    STDMETHOD(RunControlPanel)(HWND owner, DWORD flags) PURE;
    STDMETHOD(Initialize)(HINSTANCE instance, DWORD version) PURE;
};

typedef IDirectInputA* LPDIRECTINPUTA;
typedef IDirectInputA* LPDIRECTINPUT;
typedef IDirectInputDeviceA* LPDIRECTINPUTDEVICEA;
typedef IDirectInputDeviceA* LPDIRECTINPUTDEVICE;
typedef IDirectInputDevice2A* LPDIRECTINPUTDEVICE2A;

extern "C" HRESULT WINAPI DirectInputCreateA(HINSTANCE instance, DWORD version, LPDIRECTINPUTA* ppDI, LPUNKNOWN outer);

// Key codes (keyboard scan codes)
#define DIK_ESCAPE 0x01
#define DIK_1 0x02
#define DIK_2 0x03
#define DIK_3 0x04
#define DIK_4 0x05
#define DIK_5 0x06
#define DIK_6 0x07
#define DIK_7 0x08
#define DIK_8 0x09
#define DIK_9 0x0A
#define DIK_0 0x0B
#define DIK_MINUS 0x0C
#define DIK_EQUALS 0x0D
#define DIK_BACK 0x0E
#define DIK_BACKSPACE DIK_BACK
#define DIK_TAB 0x0F
#define DIK_Q 0x10
#define DIK_W 0x11
#define DIK_E 0x12
#define DIK_R 0x13
#define DIK_T 0x14
#define DIK_Y 0x15
#define DIK_U 0x16
#define DIK_I 0x17
#define DIK_O 0x18
#define DIK_P 0x19
#define DIK_LBRACKET 0x1A
#define DIK_RBRACKET 0x1B
#define DIK_RETURN 0x1C
#define DIK_LCONTROL 0x1D
#define DIK_A 0x1E
#define DIK_S 0x1F
#define DIK_D 0x20
#define DIK_F 0x21
#define DIK_G 0x22
#define DIK_H 0x23
#define DIK_J 0x24
#define DIK_K 0x25
#define DIK_L 0x26
#define DIK_SEMICOLON 0x27
#define DIK_APOSTROPHE 0x28
#define DIK_GRAVE 0x29
#define DIK_LSHIFT 0x2A
#define DIK_BACKSLASH 0x2B
#define DIK_Z 0x2C
#define DIK_X 0x2D
#define DIK_C 0x2E
#define DIK_V 0x2F
#define DIK_B 0x30
#define DIK_N 0x31
#define DIK_M 0x32
#define DIK_COMMA 0x33
#define DIK_PERIOD 0x34
#define DIK_SLASH 0x35
#define DIK_RSHIFT 0x36
#define DIK_MULTIPLY 0x37
#define DIK_LMENU 0x38
#define DIK_SPACE 0x39
#define DIK_CAPITAL 0x3A
#define DIK_F1 0x3B
#define DIK_F2 0x3C
#define DIK_F3 0x3D
#define DIK_F4 0x3E
#define DIK_F5 0x3F
#define DIK_F6 0x40
#define DIK_F7 0x41
#define DIK_F8 0x42
#define DIK_F9 0x43
#define DIK_F10 0x44
#define DIK_NUMPAD7 0x47
#define DIK_NUMPAD8 0x48
#define DIK_NUMPAD9 0x49
#define DIK_SUBTRACT 0x4A
#define DIK_NUMPAD4 0x4B
#define DIK_NUMPAD5 0x4C
#define DIK_NUMPAD6 0x4D
#define DIK_ADD 0x4E
#define DIK_NUMPAD1 0x4F
#define DIK_NUMPAD2 0x50
#define DIK_NUMPAD3 0x51
#define DIK_NUMPAD0 0x52
#define DIK_DECIMAL 0x53
#define DIK_F11 0x57
#define DIK_F12 0x58
#define DIK_NUMPADENTER 0x9C
#define DIK_RCONTROL 0x9D
#define DIK_DIVIDE 0xB5
#define DIK_SYSRQ 0xB7
#define DIK_RMENU 0xB8
#define DIK_PAUSE 0xC5
#define DIK_HOME 0xC7
#define DIK_UP 0xC8
#define DIK_PRIOR 0xC9
#define DIK_LEFT 0xCB
#define DIK_RIGHT 0xCD
#define DIK_END 0xCF
#define DIK_DOWN 0xD0
#define DIK_NEXT 0xD1
#define DIK_INSERT 0xD2
#define DIK_DELETE 0xD3
#define DIK_LWIN 0xDB
#define DIK_RWIN 0xDC
#define DIK_APPS 0xDD
#define DIK_OEM_102 0x56
