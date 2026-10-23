// Port (docs/port.md): the user32 subset of sdl_win32.hpp on SDL3, for the game's one window.
#include "sdl_win32.hpp"
#include <SDL3/SDL.h>
#include <deque>

void Port_DInputOnEvent(const SDL_Event& e); // sdl_dinput.cpp

namespace
{
SDL_Window* gWindow;
HWND gHwnd; // what the game sees: the native HWND on Windows (DirectInput still needs it)
WNDPROC gWndProc;
std::deque<MSG> gQueue;
bool gQuitPosted;
int gQuitCode;
int gCursorCount; // ShowCursor's display counter

void Post(UINT message, WPARAM wParam = 0, LPARAM lParam = 0)
{
    MSG msg = {};
    msg.hwnd = gHwnd;
    msg.message = message;
    msg.wParam = wParam;
    msg.lParam = lParam;
    gQueue.push_back(msg);
}

// The window messages WindowProc_5E4EE0 acts on, from an SDL event.
void Translate(const SDL_Event& e)
{
    Port_DInputOnEvent(e);
    switch (e.type)
    {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            Post(WM_CLOSE);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            Post(WM_ACTIVATE, WA_ACTIVE);
            Post(WM_SETFOCUS);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            Post(WM_ACTIVATE, WA_INACTIVE);
            Post(WM_KILLFOCUS);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            Post(WM_SIZE, SIZE_MINIMIZED);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            Post(WM_SIZE, SIZE_RESTORED);
            break;
        case SDL_EVENT_WINDOW_MOVED:
            Post(WM_WINDOWPOSCHANGED);
            break;
        case SDL_EVENT_KEY_DOWN:
            // Alt+Enter switches between windowed and full screen (bit 29: Alt held)
            if (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))
            {
                Post(WM_SYSKEYDOWN, VK_RETURN, 0x20000000);
            }
            break;
        default:
            break;
    }
}
} // namespace

void* Port_GetSDLWindow()
{
    return gWindow;
}

ATOM Port_RegisterClassA(const WNDCLASSA* pClass)
{
    gWndProc = pClass->lpfnWndProc;
    return 1;
}

HWND Port_CreateWindowExA(DWORD, LPCSTR, LPCSTR title, DWORD, int, int, int w, int h, HWND, HMENU, HINSTANCE, LPVOID)
{
    const bool headless = SDL_getenv("GTA2_HEADLESS") != NULL; // no window content, no sound (docs/port.md, M2)
    if (headless)
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        return NULL;
    }
    // The GL attributes take effect when the window is created (its pixel format on Windows).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    gWindow = SDL_CreateWindow(title, w, h, headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE); // resizable: the renderer scales the picture (GLRenderer::Present)
    if (!gWindow)
    {
        return NULL;
    }
#if defined(_WIN32)
    gHwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(gWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#endif
    if (!gHwnd)
    {
        gHwnd = (HWND)gWindow; // no native window (headless, other platforms): only these functions use it
    }
    return gHwnd;
}

BOOL Port_DestroyWindow(HWND)
{
    if (!gWindow)
    {
        return FALSE;
    }
    MSG msg = {};
    msg.hwnd = gHwnd;
    msg.message = WM_DESTROY;
    Port_DispatchMessageA(&msg); // the game shuts down its renderer here, before the window goes
    SDL_DestroyWindow(gWindow);
    gWindow = NULL;
    return TRUE;
}

BOOL Port_ShowWindow(HWND, int cmd)
{
    if (!gWindow)
    {
        return FALSE;
    }
    if (cmd == SW_SHOWMINNOACTIVE || cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED)
    {
        SDL_MinimizeWindow(gWindow);
    }
    else if (cmd != SW_HIDE)
    {
        SDL_ShowWindow(gWindow);
    }
    return TRUE;
}

BOOL Port_UpdateWindow(HWND)
{
    return TRUE;
}

// The SDL window has no frame the game needs to know about: the window rect is the client area.
BOOL Port_GetClientRect(HWND, LPRECT pRect)
{
    int w = 0, h = 0;
    SDL_GetWindowSize(gWindow, &w, &h);
    pRect->left = pRect->top = 0;
    pRect->right = w;
    pRect->bottom = h;
    return TRUE;
}

BOOL Port_GetWindowRect(HWND, LPRECT pRect)
{
    int x = 0, y = 0, w = 0, h = 0;
    SDL_GetWindowPosition(gWindow, &x, &y);
    SDL_GetWindowSize(gWindow, &w, &h);
    pRect->left = x;
    pRect->top = y;
    pRect->right = x + w;
    pRect->bottom = y + h;
    return TRUE;
}

BOOL Port_SetWindowPos(HWND, HWND, int x, int y, int cx, int cy, UINT flags)
{
    if (!gWindow || (SDL_GetWindowFlags(gWindow) & SDL_WINDOW_FULLSCREEN))
    {
        return TRUE;
    }
    // The game sizes its window to the video mode, again on every mode reset (when it gets focus back). The renderer
    // scales the picture to the window (GLRenderer::Present), so only the first size is taken: after that the window
    // keeps the size its user gave it (set again, it and its drawable disagreed until it was resized by hand)
    static bool sSized;
    if (!(flags & SWP_NOSIZE) && cx > 0 && cy > 0 && !sSized)
    {
        SDL_SetWindowSize(gWindow, cx, cy);
        sSized = true;
    }
    if (!(flags & SWP_NOMOVE))
    {
        SDL_SetWindowPosition(gWindow, x, y);
    }
    return TRUE;
}

// The game sets WS_VISIBLE alone for full screen and adds the overlapped window styles for windowed.
LONG Port_SetWindowLongA(HWND, int index, LONG value)
{
    if (gWindow && index == GWL_STYLE)
    {
        SDL_SetWindowFullscreen(gWindow, !(value & WS_CAPTION));
    }
    return 0;
}

int Port_GetSystemMetrics(int index)
{
    const SDL_DisplayMode* pMode = SDL_GetDesktopDisplayMode(gWindow ? SDL_GetDisplayForWindow(gWindow) : SDL_GetPrimaryDisplay());
    if (!pMode)
    {
        return 0;
    }
    return index == SM_CXSCREEN ? pMode->w : index == SM_CYSCREEN ? pMode->h : 0;
}

// InitVideo_5D96C0 only offers windowed mode on a 16-bit desktop, which DMA's DirectDraw renderer
// needed. The GL renderer draws 16-bit frames on any desktop.
int Port_GetDeviceCaps(HDC, int index)
{
    return index == BITSPIXEL ? 16 : 0;
}

int Port_ShowCursor(BOOL show)
{
    gCursorCount += show ? 1 : -1;
    if (gCursorCount >= 0)
    {
        SDL_ShowCursor();
    }
    else
    {
        SDL_HideCursor();
    }
    return gCursorCount;
}

int Port_MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, caption, text, gWindow);
    return IDOK;
}

// The game's next frame time (Source/winmain.cpp: RunGameFrame_4DA850 runs a frame once timeGetTime reaches it)
extern unsigned int gMatchStartTime_6F5A28;

BOOL Port_PeekMessageA(LPMSG pMsg, HWND, UINT, UINT, UINT)
{
    SDL_Event e;
    while (gQueue.empty() && !gQuitPosted && SDL_PollEvent(&e))
    {
        Translate(e);
    }
    if (!gQueue.empty())
    {
        *pMsg = gQueue.front();
        gQueue.pop_front();
        return TRUE;
    }
    if (gQuitPosted)
    {
        MSG msg = {};
        msg.message = WM_QUIT;
        msg.wParam = gQuitCode;
        *pMsg = msg;
        return TRUE;
    }
    // Nothing to do until the next frame: the game's loop would spin on PeekMessageA and RunGameFrame_4DA850 and
    // keep a core busy (as the original does). A millisecond's sleep while a frame is more than 1 ms away; never
    // when one is due (lockstep runs, replays, the frontend's own timing), so nothing runs slower.
    if ((int)(gMatchStartTime_6F5A28 - timeGetTime()) > 1)
    {
        SDL_Delay(1);
    }
    return FALSE;
}

BOOL Port_TranslateMessage(const MSG*)
{
    return FALSE;
}

LRESULT Port_DispatchMessageA(const MSG* pMsg)
{
    return gWndProc ? gWndProc(pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam) : 0;
}

LRESULT Port_DefWindowProcA(HWND hWnd, UINT msg, WPARAM, LPARAM)
{
    if (msg == WM_CLOSE)
    {
        Port_DestroyWindow(hWnd);
    }
    return 0;
}

void Port_PostQuitMessage(int code)
{
    gQuitPosted = true;
    gQuitCode = code;
}
