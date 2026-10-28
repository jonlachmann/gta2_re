// Hooks GetDeviceData and GetDeviceState in the DirectInput keyboard vtable (shared by the game's device): drops
// NumLock and Scroll Lock events (see gDropLockKeys), and runs the scripted keys and key log of dev/input_script.cpp.
#include "dinput_hook.hpp"
#include "../dev/input_script.hpp"
#include <stdlib.h>
#include <string.h>

namespace
{
typedef HRESULT(__stdcall* TGetDeviceData)(IDirectInputDeviceA*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
typedef HRESULT(__stdcall* TGetDeviceState)(IDirectInputDeviceA*, DWORD, LPVOID);

TGetDeviceData gRealGetDeviceData;
TGetDeviceState gRealGetDeviceState;
// Wine on XWayland can flood the keyboard buffer with NumLock or Scroll Lock pairs; GTA2 reads one event per
// frame, so they delay every real key. GTA2 uses neither key. Opt-out: GTA2GL_KEEP_LOCK_KEYS=1.
bool gDropLockKeys;

bool IsDroppedKey(DWORD dik)
{
    return gDropLockKeys && (dik == DIK_NUMLOCK || dik == DIK_SCROLL);
}

bool IsKeyboard(IDirectInputDeviceA* pDev)
{
    static IDirectInputDeviceA* sKnown[8];
    static bool sIsKb[8];
    for (int i = 0; i < 8; i++)
    {
        if (sKnown[i] == pDev)
        {
            return sIsKb[i];
        }
    }
    DIDEVCAPS caps;
    memset(&caps, 0, sizeof(caps));
    caps.dwSize = sizeof(DIDEVCAPS_DX3);
    const bool kb = SUCCEEDED(pDev->GetCapabilities(&caps)) && (caps.dwDevType & 0xFF) == DIDEVTYPE_KEYBOARD;
    for (int i = 0; i < 8; i++)
    {
        if (!sKnown[i])
        {
            sKnown[i] = pDev;
            sIsKb[i] = kb;
            break;
        }
    }
    return kb;
}

// The real read, without the dropped lock keys
HRESULT ReadKeyboard(IDirectInputDeviceA* pDev, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags)
{
    const DWORD requested = (pdwInOut && rgdod && !(dwFlags & DIGDD_PEEK)) ? *pdwInOut : 0;
    HRESULT hr = gRealGetDeviceData(pDev, cbObjectData, rgdod, pdwInOut, dwFlags);
    for (int i = 0; i < 256 && requested == 1 && SUCCEEDED(hr) && *pdwInOut == 1 && IsDroppedKey(rgdod->dwOfs); i++)
    {
        InputScript_LogDropped(*rgdod);
        *pdwInOut = 1;
        hr = gRealGetDeviceData(pDev, cbObjectData, rgdod, pdwInOut, dwFlags);
    }
    if (InputScript_Logging() && rgdod && SUCCEEDED(hr) && pdwInOut && *pdwInOut > 0)
    {
        DWORD left = INFINITE;
        gRealGetDeviceData(pDev, cbObjectData, 0, &left, DIGDD_PEEK);
        InputScript_LogRead(*rgdod, left);
    }
    return hr;
}

HRESULT __stdcall HookGetDeviceData(IDirectInputDeviceA* pDev,
                                    DWORD cbObjectData,
                                    LPDIDEVICEOBJECTDATA rgdod,
                                    LPDWORD pdwInOut,
                                    DWORD dwFlags)
{
    if (!IsKeyboard(pDev))
    {
        return gRealGetDeviceData(pDev, cbObjectData, rgdod, pdwInOut, dwFlags);
    }
    if (!InputScript_Scripted())
    {
        return ReadKeyboard(pDev, cbObjectData, rgdod, pdwInOut, dwFlags);
    }
    // Scripted keys come first, one per read, then the real ones
    const DWORD due = InputScript_Due();
    if (!rgdod || (dwFlags & DIGDD_PEEK))
    {
        HRESULT hr = gRealGetDeviceData(pDev, cbObjectData, rgdod, pdwInOut, dwFlags);
        if (FAILED(hr) && pdwInOut)
        {
            *pdwInOut = 0;
        }
        if (pdwInOut && (dwFlags & DIGDD_PEEK))
        {
            *pdwInOut += due;
        }
        return DI_OK;
    }
    if (due && pdwInOut && *pdwInOut >= 1 && InputScript_Take(rgdod, cbObjectData))
    {
        *pdwInOut = 1;
        return DI_OK;
    }
    HRESULT hr = ReadKeyboard(pDev, cbObjectData, rgdod, pdwInOut, dwFlags);
    if (FAILED(hr) && pdwInOut)
    {
        *pdwInOut = 0;
        return DI_OK;
    }
    return hr;
}

// Immediate mode (the frontend polls GetDeviceState): the scripted keys held between their down and up frames.
HRESULT __stdcall HookGetDeviceState(IDirectInputDeviceA* pDev, DWORD size, LPVOID data)
{
    HRESULT hr = gRealGetDeviceState(pDev, size, data);
    if (!InputScript_Scripted() || size != 256 || !data || !IsKeyboard(pDev))
    {
        return hr;
    }
    if (FAILED(hr))
    {
        memset(data, 0, size);
        hr = DI_OK;
    }
    InputScript_HoldKeys((BYTE*)data);
    return hr;
}
} // namespace

void InstallDInputHook(const unsigned* pFrameCounter)
{
    if (gRealGetDeviceData)
    {
        return;
    }
    gDropLockKeys = getenv("GTA2GL_KEEP_LOCK_KEYS") == NULL;
    if (!InputScript_Init(pFrameCounter) && !gDropLockKeys)
    {
        return;
    }

    // Any keyboard device created here shares its vtable with the game's.
    typedef HRESULT(WINAPI * TCreate)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
    HMODULE hDi = LoadLibraryA("dinput.dll");
    TCreate pCreate = hDi ? (TCreate)GetProcAddress(hDi, "DirectInputCreateA") : 0;
    LPDIRECTINPUTA pDi = 0;
    LPDIRECTINPUTDEVICEA pDev = 0;
    if (!pCreate || FAILED(pCreate(GetModuleHandleA(0), 0x700, &pDi, 0)) || FAILED(pDi->CreateDevice(GUID_SysKeyboard, &pDev, 0)))
    {
        OutputDebugStringA("dinput_hook: DirectInput unavailable\n");
        return;
    }
    void** vtbl = *(void***)pDev;
    DWORD old;
    VirtualProtect(&vtbl[10], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    gRealGetDeviceData = (TGetDeviceData)vtbl[10];
    vtbl[10] = (void*)HookGetDeviceData;
    VirtualProtect(&vtbl[10], sizeof(void*), old, &old);
    VirtualProtect(&vtbl[9], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    gRealGetDeviceState = (TGetDeviceState)vtbl[9];
    vtbl[9] = (void*)HookGetDeviceState;
    VirtualProtect(&vtbl[9], sizeof(void*), old, &old);
    // pDev and pDi stay alive so the vtable stays valid.
}
