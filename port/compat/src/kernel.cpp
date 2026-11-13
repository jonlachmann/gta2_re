// Kernel, shell and COM functions the game's platform code calls, on every platform: handles,
// timers, debug output, the CD check, and no network play.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <SDL3/SDL.h>
#include "port_platform.hpp"
#include <stdio.h>
#include <stdlib.h>

namespace
{
enum class HandleKind
{
    Mutex,
    Event,
};

struct Handle
{
    HandleKind mKind;
};
} // namespace

// GTA2_SPEED=<k>: the game's clock k times faster; a large k runs a network game's steps back to back (a test, the
// bots' training)
double port_ClockSpeed()
{
    const char* speed = SDL_getenv("GTA2_SPEED");
    static const double sSpeed = speed ? atof(speed) : 1.0;
    return sSpeed > 0 ? sSpeed : 1.0;
}

// GTA2_HEADLESS: no window content, no sound, no gamepads (a dedicated server, the tests). Both read once, after main.cpp
// gave the network game's command line its say
bool port_Headless()
{
    static const bool sHeadless = SDL_getenv("GTA2_HEADLESS") != NULL; // SDL's: a server sets it (port/net/net_main.cpp)
    return sHeadless;
}

extern "C" {

BOOL WINAPI CloseHandle(HANDLE h)
{
    delete static_cast<Handle*>(h);
    return TRUE;
}

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR)
{
    return new Handle{HandleKind::Event};
}

HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR)
{
    return new Handle{HandleKind::Mutex};
}

// One instance per process: no other game holds the mutex.
HANDLE WINAPI OpenMutexA(DWORD, BOOL, LPCSTR)
{
    return NULL;
}

BOOL WINAPI ReleaseMutex(HANDLE)
{
    return TRUE;
}

void WINAPI Sleep(DWORD ms)
{
    SDL_Delay((Uint32)(ms / port_ClockSpeed()));
}

DWORD WINAPI GetTickCount()
{
    return timeGetTime();
}

// In ns: at a large GTA2_SPEED a millisecond holds many steps. Wraps after 49 days / k (71 minutes at k = 1000).
DWORD WINAPI timeGetTime()
{
    return (DWORD)(Uint64)(SDL_GetTicksNS() / 1e6 * port_ClockSpeed());
}

void WINAPI OutputDebugStringA(LPCSTR s)
{
    static const bool sEnabled = getenv("GTA2_DEBUG_OUTPUT") != NULL;
    if (sEnabled)
    {
        SDL_Log("%s", s);
    }
}

UINT WINAPI GetDriveTypeA(LPCSTR)
{
    return DRIVE_FIXED;
}

HINSTANCE WINAPI ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT)
{
    return (HINSTANCE)(intptr_t)SE_ERR_FNF;
}

HRESULT WINAPI CoInitialize(LPVOID)
{
    return S_OK;
}

void WINAPI CoUninitialize()
{
}

// Only DirectPlay is created through COM, and the port has no network play.
HRESULT WINAPI CoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID* ppv)
{
    *ppv = NULL;
    return REGDB_E_CLASSNOTREG;
}

} // extern "C"
