// Windows build: renames the Win32 and DirectX functions the game calls to their port/compat
// implementations on SDL3, so it runs the same code as on the other platforms. Force-included
// before any header; the others (files, strings, version) stay Windows'.
#pragma once

// Window, message loop and dialogs
#define RegisterClassA port_RegisterClassA
#define CreateWindowExA port_CreateWindowExA
#define DestroyWindow port_DestroyWindow
#define DefWindowProcA port_DefWindowProcA
#define CallWindowProcA port_CallWindowProcA
#define PeekMessageA port_PeekMessageA
#define TranslateMessage port_TranslateMessage
#define DispatchMessageA port_DispatchMessageA
#define PostMessageA port_PostMessageA
#define SendMessageA port_SendMessageA
#define PostQuitMessage port_PostQuitMessage
#define ShowWindow port_ShowWindow
#define UpdateWindow port_UpdateWindow
#define SetWindowPos port_SetWindowPos
#define GetClientRect port_GetClientRect
#define GetWindowRect port_GetWindowRect
#define GetWindowLongA port_GetWindowLongA
#define SetWindowLongA port_SetWindowLongA
#define GetSystemMetrics port_GetSystemMetrics
#define SetForegroundWindow port_SetForegroundWindow
#define FindWindowExA port_FindWindowExA
#define ShowCursor port_ShowCursor
#define LoadIconA port_LoadIconA
#define LoadCursorA port_LoadCursorA
#define GetDC port_GetDC
#define ReleaseDC port_ReleaseDC
#define GetDeviceCaps port_GetDeviceCaps
#define SetTimer port_SetTimer
#define KillTimer port_KillTimer
#define SetWindowTextA port_SetWindowTextA
#define GetWindowTextA port_GetWindowTextA
#define MessageBoxA port_MessageBoxA
#define DialogBoxParamA port_DialogBoxParamA
#define EndDialog port_EndDialog
#define EnableWindow port_EnableWindow
#define GetDlgItem port_GetDlgItem
#define GetDlgCtrlID port_GetDlgCtrlID
#define GetDlgItemTextA port_GetDlgItemTextA
#define SetDlgItemTextA port_SetDlgItemTextA
#define SendDlgItemMessageA port_SendDlgItemMessageA
#define GetParent port_GetParent

// DLLs
#define LoadLibraryA port_LoadLibraryA
#define GetProcAddress port_GetProcAddress
#define FreeLibrary port_FreeLibrary

// DirectX and COM
#define DirectInputCreateA port_DirectInputCreateA
#define DirectPlayCreate port_DirectPlayCreate
#define DirectPlayLobbyCreateW port_DirectPlayLobbyCreateW
#define CoInitialize port_CoInitialize
#define CoUninitialize port_CoUninitialize
#define CoCreateInstance port_CoCreateInstance

// Registry
#define RegOpenKeyExA port_RegOpenKeyExA
#define RegOpenKeyA port_RegOpenKeyA
#define RegCreateKeyExA port_RegCreateKeyExA
#define RegCloseKey port_RegCloseKey
#define RegQueryValueExA port_RegQueryValueExA
#define RegSetValueExA port_RegSetValueExA
#define RegDeleteValueA port_RegDeleteValueA

// Kernel and shell
#define CloseHandle port_CloseHandle
#define CreateEventA port_CreateEventA
#define CreateMutexA port_CreateMutexA
#define OpenMutexA port_OpenMutexA
#define ReleaseMutex port_ReleaseMutex
#define Sleep port_Sleep
#define GetTickCount port_GetTickCount
#define timeGetTime port_timeGetTime
#define OutputDebugStringA port_OutputDebugStringA
#define GetDriveTypeA port_GetDriveTypeA
#define ShellExecuteA port_ShellExecuteA

// Files: only paths on a drive letter change, see crt.cpp. MinGW's _findfirst is _findfirst64i32.
#include <io.h>
#include <stdint.h>
#include <stdio.h>
FILE* port_fopen(const char* path, const char* mode);
intptr_t port_findfirst(const char* pattern, struct _finddata_t* pData);
#undef _findfirst
#define fopen port_fopen
#define _findfirst port_findfirst

#if defined(_WIN64) && defined(__cplusplus)
// The network lobby (never shown: no network play) uses 32-bit Windows' GWL_WNDPROC, which winuser.h
// removes on 64-bit, and an int dialog procedure.
enum { GWL_WNDPROC = -4 };
template <typename H, typename U, typename W, typename LP, typename I, typename T, typename P, typename L>
inline auto port_DialogBoxParamA(I instance, T name, P parent, int (*proc)(H, U, W, LP), L param)
{
    return port_DialogBoxParamA(instance, name, (H)parent, reinterpret_cast<long long (*)(H, U, W, LP)>(proc), (LP)param);
}
#endif
