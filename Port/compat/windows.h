// Port (docs/port.md): the Windows names the game uses, for non-Windows builds (only those look in
// Port/compat). Types, constants and declarations only; the few functions the game still calls are in
// Port/posix_win32.cpp. 32-bit only, like the port: LONG is 32 bits, pointers are 4 bytes.
#pragma once

#include <stdarg.h> // windows.h brings these in, and the game relies on it
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define _HRESULT_DEFINED // HRESULT is ours; ddraw_vc6.h would otherwise typedef it again
#define _Return_type_success_(x)
#define _Check_return_

#define WINAPI __attribute__((stdcall))
#define APIENTRY WINAPI
#define CALLBACK __attribute__((stdcall))
#define PASCAL __attribute__((stdcall))
#define FAR
#define NEAR
#define CONST const
#define VOID void
#ifndef TRUE
    #define TRUE 1
    #define FALSE 0
#endif
#define MAX_PATH 260

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD; // 32-bit, as on Windows (not unsigned long, which is 64-bit on LP64)
typedef int LONG;
typedef unsigned int ULONG;
typedef unsigned int UINT;
typedef int INT;
typedef char CHAR;
typedef short SHORT;
typedef unsigned short USHORT;
typedef unsigned char UCHAR;
typedef float FLOAT;
typedef wchar_t WCHAR;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef ULONG_PTR DWORD_PTR;
typedef ULONG_PTR SIZE_T;
typedef LONG HRESULT;
typedef LONG_PTR LRESULT;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef WORD ATOM;
typedef DWORD COLORREF;

typedef void* LPVOID;
typedef const void* LPCVOID;
typedef void* PVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef BYTE* LPBYTE;
typedef BYTE* PBYTE;
typedef DWORD* LPDWORD;
typedef DWORD* PDWORD;
typedef WORD* LPWORD;
typedef LONG* LPLONG;
typedef BOOL* LPBOOL;
typedef int* LPINT;

typedef void* HANDLE;
#define DECLARE_HANDLE(name) typedef struct name##__* name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HGDIOBJ);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HMONITOR);
typedef HICON HCURSOR;
typedef HINSTANCE HMODULE;
typedef HKEY* PHKEY;
typedef DWORD REGSAM;
typedef int (WINAPI* FARPROC)();
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct _GUID
{
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} GUID;
typedef GUID IID;
typedef GUID CLSID;
typedef GUID* LPGUID;
typedef const GUID* LPCGUID;
#ifdef __cplusplus
    #define REFGUID const GUID&
    #define REFIID const IID&
    #define REFCLSID const CLSID&
inline bool IsEqualGUID(REFGUID a, REFGUID b)
{
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}
inline bool operator==(REFGUID a, REFGUID b)
{
    return IsEqualGUID(a, b);
}
#endif

typedef union _LARGE_INTEGER
{
    struct
    {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct tagRECT
{
    LONG left, top, right, bottom;
} RECT, *LPRECT, *PRECT;
typedef const RECT* LPCRECT;
typedef struct tagPOINT
{
    LONG x, y;
} POINT, *LPPOINT;
typedef struct tagSIZE
{
    LONG cx, cy;
} SIZE, *LPSIZE;
typedef struct tagPALETTEENTRY
{
    BYTE peRed, peGreen, peBlue, peFlags;
} PALETTEENTRY, *LPPALETTEENTRY;
typedef struct tagRGBQUAD
{
    BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;
typedef struct tagMSG
{
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *LPMSG;
typedef struct tagWNDCLASSA
{
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
} WNDCLASSA;
typedef struct tagWINDOWPOS
{
    HWND hwnd, hwndInsertAfter;
    int x, y, cx, cy;
    UINT flags;
} WINDOWPOS;
typedef struct _SECURITY_ATTRIBUTES
{
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef struct _SYSTEMTIME
{
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)((DWORD)(l) >> 16))
#define LOBYTE(w) ((BYTE)((w) & 0xFF))
#define HIBYTE(w) ((BYTE)(((WORD)(w)) >> 8))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_POINTER ((HRESULT)0x80004003)
#define E_HANDLE ((HRESULT)0x80070006)
#define E_ACCESSDENIED ((HRESULT)0x80070005)
#define E_PENDING ((HRESULT)0x8000000A)
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110)
#define CO_E_NOTINITIALIZED ((HRESULT)0x800401F0)
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154)
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_MORE_DATA 234L
#define ERROR_ALREADY_EXISTS 183L
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INFINITE 0xFFFFFFFF

// Window messages and styles the game and Port/sdl_win32.cpp use
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_SIZING 0x0214
#define WM_USER 0x0400
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SC_SIZE 0xF000
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_F10 0x79
#define VK_F12 0x7B
#define VK_LMENU 0xA4
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_RESTORE 9
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define GWL_STYLE (-16)
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define BITSPIXEL 12
#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_YESNO 0x00000004L
#define MB_ICONERROR 0x00000010L
#define MB_ICONHAND MB_ICONERROR
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONINFORMATION 0x00000040L
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define IDC_ARROW ((LPCSTR)32512)
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))

// Registry (Port/registry_ini.cpp)
#define HKEY_CLASSES_ROOT ((HKEY)(ULONG_PTR)0x80000000)
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002)
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_OPTION_NON_VOLATILE 0
#define REG_CREATED_NEW_KEY 1
#define REG_OPENED_EXISTING_KEY 2

#include "port_win32_functions.h"
#include "objbase.h"
#include "mmsystem.h"
