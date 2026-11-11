// The part of the Win32 API the game and port/compat use, for platforms without Windows headers.
// Declarations only: port/compat/src implements the functions. Layouts are the 32-bit Windows ones.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#if defined(__i386__) || defined(_M_IX86)
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define CALLBACK WINAPI
#define APIENTRY WINAPI
#define PASCAL WINAPI
#define WINAPIV
#define CDECL
#define FAR
#define NEAR
#define CONST const
#define STRICT
#define VOID void
#define EXTERN_C extern "C"

// Types

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
// 32 bits on every target, as on 64-bit Windows: the game's files and saves hold them.
#if defined(__LP64__)
typedef unsigned int DWORD;
typedef int LONG;
typedef unsigned int ULONG;
#else
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long ULONG;
#endif
typedef char CHAR;
typedef short SHORT;
typedef unsigned short USHORT;
typedef int INT;
typedef unsigned int UINT;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef float FLOAT;
typedef unsigned char BOOLEAN;
typedef wchar_t WCHAR;
#if defined(__LP64__)
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
#else
typedef int INT_PTR;
typedef unsigned int UINT_PTR;
typedef long LONG_PTR;
typedef unsigned long ULONG_PTR;
#endif
typedef ULONG_PTR DWORD_PTR;
typedef ULONG_PTR SIZE_T;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef WORD ATOM;
typedef LONG HRESULT;
typedef LONG LSTATUS;
typedef DWORD REGSAM;
typedef DWORD COLORREF;
typedef void* HANDLE;

typedef void* LPVOID;
typedef const void* LPCVOID;
typedef CHAR* LPSTR;
typedef const CHAR* LPCSTR;
typedef WCHAR* LPWSTR;
typedef const WCHAR* LPCWSTR;
typedef BYTE* LPBYTE;
typedef BYTE* PBYTE;
typedef WORD* LPWORD;
typedef DWORD* LPDWORD;
typedef DWORD* PDWORD;
typedef LONG* LPLONG;
typedef LONG* PLONG;
typedef BOOL* LPBOOL;
typedef UINT* PUINT;
typedef HANDLE* LPHANDLE;

#define DECLARE_HANDLE(name) \
    struct name##__          \
    {                        \
        int unused;          \
    };                       \
    typedef struct name##__* name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HGLRC);
typedef HINSTANCE HMODULE;
typedef HICON HCURSOR;
typedef HKEY* PHKEY;
typedef HANDLE HGLOBAL;

typedef INT_PTR(WINAPI* FARPROC)();
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
#if defined(__LP64__)
// The network lobby's dialog procedure returns an int; the port shows no dialog.
typedef INT(CALLBACK* DLGPROC)(HWND, UINT, WPARAM, LPARAM);
#else
typedef INT_PTR(CALLBACK* DLGPROC)(HWND, UINT, WPARAM, LPARAM);
#endif
typedef void(CALLBACK* TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);

// Values and macros

#define TRUE 1
#define FALSE 0
#define MAX_PATH 260

#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xFF))
#define HIBYTE(w) ((BYTE)((DWORD_PTR)(w) >> 8))
#define MAKEWORD(low, high) ((WORD)(((BYTE)((DWORD_PTR)(low) & 0xFF)) | ((WORD)((BYTE)((DWORD_PTR)(high) & 0xFF))) << 8))
#define MAKELONG(low, high) ((LONG)(((WORD)((DWORD_PTR)(low) & 0xFFFF)) | ((DWORD)((WORD)((DWORD_PTR)(high) & 0xFFFF))) << 16))
#define MAKELPARAM(low, high) ((LPARAM)(DWORD)MAKELONG(low, high))
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEFOURCC(c0, c1, c2, c3) ((DWORD)(BYTE)(c0) | ((DWORD)(BYTE)(c1) << 8) | ((DWORD)(BYTE)(c2) << 16) | ((DWORD)(BYTE)(c3) << 24))
#define mmioFOURCC(c0, c1, c2, c3) MAKEFOURCC(c0, c1, c2, c3)

#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_FAIL ((HRESULT)0x80004005L)
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154L)
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr) ((HRESULT)(hr) < 0)
#define MAKE_HRESULT(sev, fac, code) ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#define SEVERITY_ERROR 1
#define FACILITY_WIN32 7

#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_INVALID_ACCESS 12L
#define ERROR_READ_FAULT 30L
#define ERROR_MORE_DATA 234L

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define DRIVE_FIXED 3
#define DRIVE_CDROM 5
#define CP_ACP 0
#define MUTEX_ALL_ACCESS 0x001F0001
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT 2
#define KL_NAMELENGTH 9
#define LANG_GERMAN 0x07
#define LANG_SPANISH 0x0a
#define LANG_FRENCH 0x0c
#define LANG_ITALIAN 0x10
#define LANG_PORTUGUESE 0x16
#define LANG_RUSSIAN 0x19

#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002)
#define KEY_ALL_ACCESS 0x000F003F
#define KEY_WRITE 0x00020006
#define REG_SZ 1
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_OPTION_NON_VOLATILE 0
#define REG_CREATED_NEW_KEY 1
#define REG_OPENED_EXISTING_KEY 2

// Windows and messages
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000f
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ACTIVATEAPP 0x001c
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_SIZING 0x0214
#define WM_USER 0x0400
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SC_MINIMIZE 0xf020
#define SC_MAXIMIZE 0xf030
#define SC_CLOSE 0xf060
#define SC_SCREENSAVE 0xf140
#define SC_MONITORPOWER 0xf170
#define KF_MENUMODE 0x1000
#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001

#define WS_OVERLAPPED 0x00000000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_THICKFRAME 0x00040000L
#define WS_SYSMENU 0x00080000L
#define WS_DLGFRAME 0x00400000L
#define WS_BORDER 0x00800000L
#define WS_CAPTION 0x00C00000L
#define WS_VISIBLE 0x10000000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define GWLP_WNDPROC (-4)
#define GWL_WNDPROC (-4)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWLP_USERDATA (-21)
#define GWL_USERDATA (-21)
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define HWND_TOP ((HWND)0)
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define BITSPIXEL 12
#define IDC_ARROW MAKEINTRESOURCEA(32512)
#define MB_OK 0x00000000
#define MB_ICONHAND 0x00000010
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONEXCLAMATION 0x00000030
#define MB_ICONMASK 0x000000F0
#define IDOK 1
#define IDCANCEL 2
#define SE_ERR_FNF 2

#define VK_RETURN 0x0D
#define VK_MENU 0x12
#define VK_SCROLL 0x91
#define VK_F10 0x79
#define VK_LMENU 0xA4

// Dialog controls (the network lobby)
#define BM_SETCHECK 0x00f1
#define BM_GETSTATE 0x00f2
#define BST_CHECKED 0x0001
#define EM_LINESCROLL 0x00b6
#define EM_GETLINECOUNT 0x00ba
#define EN_CHANGE 0x0300
#define CB_ADDSTRING 0x0143
#define CB_GETCURSEL 0x0147
#define CB_SETCURSEL 0x014e
#define CB_GETITEMDATA 0x0150
#define CB_SETITEMDATA 0x0151
#define CBN_SELCHANGE 1
#define LB_ADDSTRING 0x0180
#define LB_RESETCONTENT 0x0184
#define LB_SETCURSEL 0x0186
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#define LB_SELECTSTRING 0x018c
#define LB_GETITEMDATA 0x0199
#define LB_SETITEMDATA 0x019a
#define LB_ERR (-1)
#define LB_ERRSPACE (-2)
#define LBN_DBLCLK 2

// Structures

typedef struct tagPOINT
{
    LONG x;
    LONG y;
} POINT, *LPPOINT;

typedef struct tagRECT
{
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *LPRECT;

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
    INT cbClsExtra;
    INT cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
} WNDCLASSA;

typedef struct tagWINDOWPOS
{
    HWND hwnd;
    HWND hwndInsertAfter;
    INT x;
    INT y;
    INT cx;
    INT cy;
    UINT flags;
} WINDOWPOS, *LPWINDOWPOS;

typedef struct _SECURITY_ATTRIBUTES
{
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _FILETIME
{
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _WIN32_FIND_DATAA
{
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

typedef struct _OSVERSIONINFOA
{
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
} OSVERSIONINFOA;

typedef struct tagVS_FIXEDFILEINFO
{
    DWORD dwSignature;
    DWORD dwStrucVersion;
    DWORD dwFileVersionMS;
    DWORD dwFileVersionLS;
    DWORD dwProductVersionMS;
    DWORD dwProductVersionLS;
    DWORD dwFileFlagsMask;
    DWORD dwFileFlags;
    DWORD dwFileOS;
    DWORD dwFileType;
    DWORD dwFileSubtype;
    DWORD dwFileDateMS;
    DWORD dwFileDateLS;
} VS_FIXEDFILEINFO;

#if defined(__i386__) || defined(_M_IX86)
static_assert(sizeof(MSG) == 28 && sizeof(WNDCLASSA) == 40 && sizeof(RECT) == 16, "Win32 layout");
static_assert(sizeof(WIN32_FIND_DATAA) == 320 && sizeof(OSVERSIONINFOA) == 148, "Win32 layout");
#endif

// COM

typedef struct _GUID
{
    DWORD Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID, *LPGUID;
typedef const GUID* LPCGUID;
static_assert(sizeof(GUID) == 16, "GUID layout");
typedef GUID IID;
typedef GUID CLSID;
typedef const GUID& REFGUID;
typedef const IID& REFIID;
typedef const CLSID& REFCLSID;

inline bool IsEqualGUID(REFGUID a, REFGUID b)
{
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}
inline bool operator==(REFGUID a, REFGUID b)
{
    return IsEqualGUID(a, b);
}
inline bool operator!=(REFGUID a, REFGUID b)
{
    return !IsEqualGUID(a, b);
}

// With INITGUID defined before the first include (port/compat/src/guids.cpp), the GUIDs are defined.
#ifdef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern "C" const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) extern "C" const GUID name
#endif

#define interface struct
#define STDMETHODCALLTYPE WINAPI
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(iface) interface iface
#define DECLARE_INTERFACE_(iface, base) interface iface : public base

DEFINE_GUID(IID_IUnknown, 0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

interface IUnknown
{
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)() PURE;
    STDMETHOD_(ULONG, Release)() PURE;
};
typedef IUnknown* LPUNKNOWN;

#define CLSCTX_INPROC_SERVER 0x1

// Functions

extern "C" {

// Kernel
BOOL WINAPI CloseHandle(HANDLE h);
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manualReset, BOOL initialState, LPCSTR name);
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCSTR name);
HANDLE WINAPI OpenMutexA(DWORD access, BOOL inherit, LPCSTR name);
BOOL WINAPI ReleaseMutex(HANDLE h);
void WINAPI Sleep(DWORD ms);
DWORD WINAPI GetTickCount();
void WINAPI OutputDebugStringA(LPCSTR s);
UINT WINAPI GetDriveTypeA(LPCSTR root);
DWORD WINAPI GetFileAttributesA(LPCSTR path);
HANDLE WINAPI FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA pData);
BOOL WINAPI FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA pData);
BOOL WINAPI FindClose(HANDLE h);
INT WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR def, LPSTR out, UINT size, LPCSTR file);
BOOL WINAPI GetUserNameA(LPSTR out, LPDWORD pSize);
BOOL WINAPI GetVersionExA(OSVERSIONINFOA* pInfo);
DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR file, LPDWORD pHandle);
BOOL WINAPI GetFileVersionInfoA(LPCSTR file, DWORD handle, DWORD size, LPVOID pData);
BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR subBlock, LPVOID* ppBuffer, PUINT pSize);
BOOL WINAPI GetKeyboardLayoutNameA(LPSTR out);
LPSTR WINAPI lstrcatA(LPSTR dst, LPCSTR src);
INT WINAPI lstrlenA(LPCSTR s);
INT WINAPI MultiByteToWideChar(UINT codePage, DWORD flags, LPCSTR src, INT srcLen, LPWSTR dst, INT dstLen);
INT WINAPI WideCharToMultiByte(UINT codePage, DWORD flags, LPCWSTR src, INT srcLen, LPSTR dst, INT dstLen, LPCSTR def, LPBOOL pUsedDef);
INT WINAPIV wsprintfA(LPSTR dst, LPCSTR fmt, ...);
INT WINAPI wvsprintfA(LPSTR dst, LPCSTR fmt, va_list args);
HMODULE WINAPI LoadLibraryA(LPCSTR name);
FARPROC WINAPI GetProcAddress(HMODULE h, LPCSTR name);
BOOL WINAPI FreeLibrary(HMODULE h);
#define LoadLibrary LoadLibraryA

// Registry
LSTATUS WINAPI RegOpenKeyA(HKEY root, LPCSTR subKey, PHKEY pResult);
LSTATUS WINAPI RegOpenKeyExA(HKEY root, LPCSTR subKey, DWORD options, REGSAM access, PHKEY pResult);
LSTATUS WINAPI RegCreateKeyExA(HKEY root, LPCSTR subKey, DWORD reserved, LPSTR className, DWORD options, REGSAM access,
                               LPSECURITY_ATTRIBUTES attributes, PHKEY pResult, LPDWORD pDisposition);
LSTATUS WINAPI RegCloseKey(HKEY h);
LSTATUS WINAPI RegQueryValueExA(HKEY h, LPCSTR name, LPDWORD reserved, LPDWORD pType, LPBYTE pData, LPDWORD pSize);
LSTATUS WINAPI RegSetValueExA(HKEY h, LPCSTR name, DWORD reserved, DWORD type, const BYTE* pData, DWORD size);
LSTATUS WINAPI RegDeleteValueA(HKEY h, LPCSTR name);

// Windows
ATOM WINAPI RegisterClassA(const WNDCLASSA* pClass);
HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, INT x, INT y, INT w, INT h,
                            HWND parent, HMENU menu, HINSTANCE instance, LPVOID param);
BOOL WINAPI DestroyWindow(HWND h);
LRESULT WINAPI DefWindowProcA(HWND h, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND h, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI PeekMessageA(MSG* pMsg, HWND h, UINT filterMin, UINT filterMax, UINT flags);
BOOL WINAPI TranslateMessage(const MSG* pMsg);
LRESULT WINAPI DispatchMessageA(const MSG* pMsg);
BOOL WINAPI PostMessageA(HWND h, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI SendMessageA(HWND h, UINT msg, WPARAM wParam, LPARAM lParam);
void WINAPI PostQuitMessage(INT code);
BOOL WINAPI ShowWindow(HWND h, INT cmd);
BOOL WINAPI UpdateWindow(HWND h);
BOOL WINAPI SetWindowPos(HWND h, HWND after, INT x, INT y, INT cx, INT cy, UINT flags);
BOOL WINAPI GetClientRect(HWND h, RECT* pRect);
BOOL WINAPI GetWindowRect(HWND h, RECT* pRect);
LONG WINAPI GetWindowLongA(HWND h, INT index);
LONG WINAPI SetWindowLongA(HWND h, INT index, LONG value);
INT WINAPI GetSystemMetrics(INT index);
BOOL WINAPI SetForegroundWindow(HWND h);
HWND WINAPI FindWindowExA(HWND parent, HWND after, LPCSTR className, LPCSTR title);
INT WINAPI ShowCursor(BOOL show);
HICON WINAPI LoadIconA(HINSTANCE instance, LPCSTR name);
HCURSOR WINAPI LoadCursorA(HINSTANCE instance, LPCSTR name);
HDC WINAPI GetDC(HWND h);
INT WINAPI ReleaseDC(HWND h, HDC dc);
INT WINAPI GetDeviceCaps(HDC dc, INT index);
UINT_PTR WINAPI SetTimer(HWND h, UINT_PTR id, UINT ms, TIMERPROC proc);
BOOL WINAPI KillTimer(HWND h, UINT_PTR id);
BOOL WINAPI SetWindowTextA(HWND h, LPCSTR text);
INT WINAPI GetWindowTextA(HWND h, LPSTR out, INT size);
INT WINAPI MessageBoxA(HWND h, LPCSTR text, LPCSTR caption, UINT type);
INT_PTR WINAPI DialogBoxParamA(HINSTANCE instance, LPCSTR templateName, HWND parent, DLGPROC proc, LPARAM param);
BOOL WINAPI EndDialog(HWND h, INT_PTR result);
BOOL WINAPI EnableWindow(HWND h, BOOL enable);
HWND WINAPI GetDlgItem(HWND h, INT id);
INT WINAPI GetDlgCtrlID(HWND h);
UINT WINAPI GetDlgItemTextA(HWND h, INT id, LPSTR out, INT size);
BOOL WINAPI SetDlgItemTextA(HWND h, INT id, LPCSTR text);
LRESULT WINAPI SendDlgItemMessageA(HWND h, INT id, UINT msg, WPARAM wParam, LPARAM lParam);
HWND WINAPI GetParent(HWND h);

// Shell, COM
HINSTANCE WINAPI ShellExecuteA(HWND h, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show);
HRESULT WINAPI CoInitialize(LPVOID reserved);
void WINAPI CoUninitialize();
HRESULT WINAPI CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID* ppv);

// The game's own entry point.
INT WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine, INT cmdShow);

} // extern "C"

#include "mmsystem.h"
