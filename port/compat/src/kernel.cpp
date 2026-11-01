// Kernel, shell and COM functions the game's platform code calls, on every platform: handles,
// timers, debug output, the CD check, and no network play.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <SDL3/SDL.h>
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
    SDL_Delay(ms);
}

DWORD WINAPI GetTickCount()
{
    return (DWORD)SDL_GetTicks();
}

DWORD WINAPI timeGetTime()
{
    return (DWORD)SDL_GetTicks();
}

void WINAPI OutputDebugStringA(LPCSTR s)
{
    static const bool sEnabled = getenv("GTA2_DEBUG_OUTPUT") != NULL;
    if (sEnabled)
    {
        fputs(s, stderr);
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
