// Port (docs/port.md): the Win32 functions of compat/posix_win32.h on POSIX and SDL3.
#if !defined(_WIN32) // non-Windows builds only
#include "compat/windows.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern "C" {

// The renderer is linked in: the game "loads" its DLLs from the program itself (gbh_graphics.cpp,
// dma_video.cpp), whose gbh_*/Vid_* functions are exported (-rdynamic).
HMODULE WINAPI LoadLibraryA(LPCSTR)
{
    return (HMODULE)dlopen(NULL, RTLD_NOW);
}

HMODULE WINAPI GetModuleHandleA(LPCSTR name)
{
    return name ? NULL : (HMODULE)dlopen(NULL, RTLD_NOW);
}

FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR name)
{
    return module ? (FARPROC)dlsym((void*)module, name) : NULL;
}

BOOL WINAPI FreeLibrary(HMODULE)
{
    return TRUE;
}

// GTA2_SPEED=<k>: the game's clock k times faster (as Port/validation/fast_clock.cpp on Windows); a large k runs a
// network game's ticks back to back (the bots' training, Port/net/session/server.cpp)
static double ClockSpeed()
{
    static const double speed = getenv("GTA2_SPEED") ? atof(getenv("GTA2_SPEED")) : 1.0;
    return speed > 0 ? speed : 1.0;
}

DWORD WINAPI timeGetTime(void)
{
    return (DWORD)(Uint64)(SDL_GetTicksNS() / 1e6 * ClockSpeed()); // ns: at a large k a millisecond holds many ticks. Wraps after 49 days / k (71 minutes at k = 1000): fine for a match
}

DWORD WINAPI GetTickCount(void)
{
    return timeGetTime();
}

void WINAPI Sleep(DWORD ms)
{
    SDL_Delay((Uint32)(ms / ClockSpeed()));
}

void WINAPI OutputDebugStringA(LPCSTR text)
{
    fputs(text, stderr);
}

// The game uses a named mutex to refuse a second instance; not on these platforms.
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR)
{
    return (HANDLE)1;
}

HANDLE WINAPI OpenMutexA(DWORD, BOOL, LPCSTR)
{
    return NULL;
}

BOOL WINAPI ReleaseMutex(HANDLE)
{
    return TRUE;
}

BOOL WINAPI CloseHandle(HANDLE)
{
    return TRUE;
}

// The data is on disk, never on a CD.
UINT WINAPI GetDriveTypeA(LPCSTR)
{
    return DRIVE_FIXED;
}

BOOL WINAPI GetComputerNameA(LPSTR buf, LPDWORD size)
{
    if (gethostname(buf, *size) != 0)
    {
        return FALSE;
    }
    buf[*size - 1] = 0;
    *size = (DWORD)strlen(buf);
    return TRUE;
}

// The login name (the replay header's, InputRecorder_67F8B0)
BOOL WINAPI GetUserNameA(LPSTR buf, LPDWORD size)
{
    const char* name = getenv("USER");
    snprintf(buf, *size, "%s", name ? name : "");
    *size = (DWORD)strlen(buf) + 1;
    return TRUE;
}

// US English (0x409): the game picks its keyboard layout table by this name.
BOOL WINAPI GetKeyboardLayoutNameA(LPSTR buf)
{
    strcpy(buf, "00000409");
    return TRUE;
}

HINSTANCE WINAPI ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT)
{
    return NULL;
}

BOOL WINAPI PostMessageA(HWND, UINT, WPARAM, LPARAM)
{
    return TRUE;
}

HICON WINAPI LoadIconA(HINSTANCE, LPCSTR)
{
    return NULL;
}

HCURSOR WINAPI LoadCursorA(HINSTANCE, LPCSTR)
{
    return NULL;
}

HDC WINAPI GetDC(HWND)
{
    return (HDC)1;
}

int WINAPI ReleaseDC(HWND, HDC)
{
    return 1;
}

char* _itoa(int value, char* buf, int radix)
{
    if (radix == 10)
    {
        sprintf(buf, "%d", value);
        return buf;
    }
    unsigned int v = (unsigned int)value;
    char tmp[40];
    int len = 0;
    do
    {
        const int d = v % radix;
        tmp[len++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= radix;
    } while (v);
    for (int i = 0; i < len; i++)
    {
        buf[i] = tmp[len - 1 - i];
    }
    buf[len] = 0;
    return buf;
}

char* _strupr(char* s)
{
    for (char* p = s; *p; p++)
    {
        *p = (char)toupper((unsigned char)*p);
    }
    return s;
}

// Latin-1 narrowing, like port_wcstombs (the game's text is 8-bit)
int WINAPI WideCharToMultiByte(UINT, DWORD, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR, LPBOOL)
{
    int n = 0;
    for (int i = 0; srcLen < 0 ? true : i < srcLen; i++)
    {
        if (n < dstLen)
        {
            dst[n] = (unsigned short)src[i] < 0x100 ? (char)src[i] : '?';
        }
        n++;
        if (srcLen < 0 && !src[i])
        {
            break;
        }
    }
    return n;
}
}
#endif
