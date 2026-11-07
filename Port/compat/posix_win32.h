// Port (docs/port.md): Win32 functions the game still calls on non-Windows builds (Port/posix_win32.cpp).
// The window ones (CreateWindowExA, PeekMessageA, ...) are Port/sdl_win32.hpp's, included by posix_prefix.h.
#pragma once
#include <strings.h>

#define DRIVE_UNKNOWN 0
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_CDROM 5
#define KL_NAMELENGTH 9
#define CP_ACP 0

#ifdef __cplusplus
extern "C" {
#endif
HMODULE WINAPI LoadLibraryA(LPCSTR name);
HMODULE WINAPI GetModuleHandleA(LPCSTR name);
FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR name);
BOOL WINAPI FreeLibrary(HMODULE module);
DWORD WINAPI timeGetTime(void);
DWORD WINAPI GetTickCount(void);
void WINAPI Sleep(DWORD ms);
void WINAPI OutputDebugStringA(LPCSTR text);
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name);
HANDLE WINAPI OpenMutexA(DWORD access, BOOL inherit, LPCSTR name);
BOOL WINAPI ReleaseMutex(HANDLE h);
BOOL WINAPI CloseHandle(HANDLE h);
UINT WINAPI GetDriveTypeA(LPCSTR root);
BOOL WINAPI GetComputerNameA(LPSTR buf, LPDWORD size);
BOOL WINAPI GetUserNameA(LPSTR buf, LPDWORD size);
BOOL WINAPI GetKeyboardLayoutNameA(LPSTR buf);
HINSTANCE WINAPI ShellExecuteA(HWND hwnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show);
BOOL WINAPI PostMessageA(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
HICON WINAPI LoadIconA(HINSTANCE hInstance, LPCSTR name);
HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR name);
HDC WINAPI GetDC(HWND hWnd);
int WINAPI ReleaseDC(HWND hWnd, HDC hdc);
char* _strupr(char* s);
int WINAPI WideCharToMultiByte(UINT codePage, DWORD flags, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR defChar,
                               LPBOOL usedDefChar);
#ifdef __cplusplus
}
#endif

#define LoadLibrary LoadLibraryA
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define wsprintfA sprintf
#define wvsprintfA vsprintf
#define lstrcatA strcat
#define lstrcpyA strcpy
#define lstrlenA strlen
