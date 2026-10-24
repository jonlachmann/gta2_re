// Port (docs/port.md): the window and message calls of winmain.cpp, on SDL3. winmain.cpp includes this
// last under GTA2_PORT, so WinMain_5E53F0 and WindowProc_5E4EE0 run unchanged: SDL events become the few
// window messages the game handles, and the window calls act on the one SDL window.
// ponytail: a user32 subset for one window and about ten messages; rewrite WinMain on SDL directly if it grows.
#pragma once

#include <windows.h>

void* Port_GetSDLWindow();
// GTA2_HEADLESS=1 (docs/port.md, M2): SDL's dummy video and audio drivers, no GL context and no drawing; the gbh_*
// layer still runs
bool Port_IsHeadless();
// The size the game last asked its window to be (SetWindowPos): its resolution in windowed mode, whatever size the
// window has (resizable: the renderer scales)
bool Port_GetRequestedWindowSize(int* pW, int* pH);

ATOM Port_RegisterClassA(const WNDCLASSA* pClass);
HWND Port_CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y, int w, int h,
                          HWND parent, HMENU menu, HINSTANCE hInstance, LPVOID param);
BOOL Port_DestroyWindow(HWND hWnd);
BOOL Port_ShowWindow(HWND hWnd, int cmd);
BOOL Port_UpdateWindow(HWND hWnd);
BOOL Port_GetClientRect(HWND hWnd, LPRECT pRect);
BOOL Port_GetWindowRect(HWND hWnd, LPRECT pRect);
BOOL Port_SetWindowPos(HWND hWnd, HWND after, int x, int y, int cx, int cy, UINT flags);
LONG Port_SetWindowLongA(HWND hWnd, int index, LONG value);
int Port_GetSystemMetrics(int index);
int Port_GetDeviceCaps(HDC hdc, int index);
int Port_ShowCursor(BOOL show);
int Port_MessageBoxA(HWND hWnd, LPCSTR text, LPCSTR caption, UINT type);
BOOL Port_PeekMessageA(LPMSG pMsg, HWND hWnd, UINT first, UINT last, UINT remove);
BOOL Port_TranslateMessage(const MSG* pMsg);
LRESULT Port_DispatchMessageA(const MSG* pMsg);
LRESULT Port_DefWindowProcA(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void Port_PostQuitMessage(int code);

#define RegisterClassA Port_RegisterClassA
#define CreateWindowExA Port_CreateWindowExA
#define DestroyWindow Port_DestroyWindow
#define ShowWindow Port_ShowWindow
#define UpdateWindow Port_UpdateWindow
#define GetClientRect Port_GetClientRect
#define GetWindowRect Port_GetWindowRect
#define SetWindowPos Port_SetWindowPos
#define SetWindowLongA Port_SetWindowLongA
#define GetSystemMetrics Port_GetSystemMetrics
#define GetDeviceCaps Port_GetDeviceCaps
#define ShowCursor Port_ShowCursor
#define MessageBoxA Port_MessageBoxA
#define PeekMessageA Port_PeekMessageA
#define TranslateMessage Port_TranslateMessage
#define DispatchMessageA Port_DispatchMessageA
#define DefWindowProcA Port_DefWindowProcA
#define PostQuitMessage Port_PostQuitMessage
