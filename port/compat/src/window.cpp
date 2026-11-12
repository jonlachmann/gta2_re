// The window and message loop on SDL3. A window is an SDL window with OpenGL; SDL events become the
// few messages the game's WindowProc handles (close, focus, minimise, Alt+Enter).
// The game's window sizes are logical (its frame size); the SDL window keeps the size the player
// gives it, and the frame is scaled into it.
#include <windows.h>
#include <SDL3/SDL.h>
#include <deque>
#include <map>
#include <string>
#include "port_platform.hpp"

namespace
{
struct Window
{
    SDL_Window* mSdl;
    WNDPROC mProc;
    LONG mStyle;
    LONG mExStyle;
    LONG_PTR mUserData;
    std::string mTitle;
    // Logical client rectangle: SDL applies sizes asynchronously, the game reads them back at once.
    int mX, mY, mW, mH;
};

std::map<std::string, WNDPROC>& Classes()
{
    static std::map<std::string, WNDPROC> sClasses;
    return sClasses;
}

std::deque<MSG>& Queue()
{
    static std::deque<MSG> sQueue;
    return sQueue;
}
Window* sMainWindow;
bool sFullscreen; // as asked: SDL reports the change later, after a resize event
int sCursorCount;

Window* W(HWND h)
{
    return reinterpret_cast<Window*>(h);
}

void Post(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MSG m = {};
    m.hwnd = h;
    m.message = msg;
    m.wParam = wParam;
    m.lParam = lParam;
    m.time = (DWORD)SDL_GetTicks();
    Queue().push_back(m);
}

void Translate(const SDL_Event& e)
{
    HWND h = reinterpret_cast<HWND>(sMainWindow);
    if (!h)
    {
        return;
    }
    switch (e.type)
    {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            Post(h, WM_CLOSE, 0, 0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            Post(h, WM_ACTIVATEAPP, TRUE, 0);
            Post(h, WM_ACTIVATE, WA_ACTIVE, 0);
            Post(h, WM_SETFOCUS, 0, 0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            Post(h, WM_ACTIVATE, WA_INACTIVE, 0);
            Post(h, WM_KILLFOCUS, 0, 0);
            Post(h, WM_ACTIVATEAPP, FALSE, 0);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            if (!sFullscreen && !(SDL_GetWindowFlags(sMainWindow->mSdl) & SDL_WINDOW_FULLSCREEN))
            {
                port_SaveWindowSize(e.window.data1, e.window.data2);
            }
            port_UpdateGameResolution();
            break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            port_UpdateGameResolution();
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            Post(h, WM_SIZE, SIZE_MINIMIZED, 0);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            Post(h, WM_SIZE, SIZE_RESTORED, MAKELPARAM(sMainWindow->mW, sMainWindow->mH));
            break;
        case SDL_EVENT_KEY_DOWN:
            // Alt+Enter: the game's full screen toggle, a WM_SYSKEYDOWN with the context bit.
            if (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT) && !e.key.repeat)
            {
                Post(h, WM_SYSKEYDOWN, VK_RETURN, 1 << 29);
            }
            break;
        default:
            break;
    }
}
} // namespace

SDL_Window* port_GetSDLWindow(HWND h)
{
    return h ? W(h)->mSdl : NULL;
}

SDL_Window* port_MainSDLWindow()
{
    return sMainWindow ? sMainWindow->mSdl : NULL;
}

void port_SetFullscreen(HWND h, bool fullscreen)
{
    if (h)
    {
        sFullscreen = fullscreen;
        if (fullscreen)
        {
            SDL_SetWindowFullscreenMode(W(h)->mSdl, port_FullscreenMode());
        }
        SDL_SetWindowFullscreen(W(h)->mSdl, fullscreen);
    }
}

extern "C" {

#if !defined(__APPLE__)
static const unsigned char kIconPng[] = {
#embed "../../resources/gta2.png"
};
#endif

ATOM WINAPI RegisterClassA(const WNDCLASSA* pClass)
{
    Classes()[pClass->lpszClassName] = pClass->lpfnWndProc;
    return 1;
}

HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, INT x, INT y, INT w, INT h, HWND, HMENU, HINSTANCE, LPVOID)
{
    auto it = Classes().find(className);
    if (it == Classes().end())
    {
        return NULL;
    }
    int windowW;
    int windowH;
    port_GetWindowSize(&windowW, &windowH);
    SDL_Window* pSdl = SDL_CreateWindow(title, windowW, windowH, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (!pSdl)
    {
        return NULL;
    }
    Window* pWin = new Window{pSdl, it->second, (LONG)style, (LONG)exStyle, 0, title ? title : "", x, y, w, h};
    if (!sMainWindow)
    {
        sMainWindow = pWin;
#if !defined(__APPLE__)
        // On macOS the window icon would replace the bundle's in the Dock.
        if (SDL_Surface* pIcon = SDL_LoadPNG_IO(SDL_IOFromConstMem(kIconPng, sizeof(kIconPng)), true))
        {
            SDL_SetWindowIcon(pSdl, pIcon);
            SDL_DestroySurface(pIcon);
        }
#endif
        port_UpdateGameResolution();
    }
    return reinterpret_cast<HWND>(pWin);
}

BOOL WINAPI DestroyWindow(HWND h)
{
    if (!h)
    {
        return FALSE;
    }
    Window* pWin = W(h);
    pWin->mProc(h, WM_DESTROY, 0, 0);
    SDL_DestroyWindow(pWin->mSdl);
    if (pWin == sMainWindow)
    {
        sMainWindow = NULL;
    }
    delete pWin;
    return TRUE;
}

LRESULT WINAPI DefWindowProcA(HWND h, UINT msg, WPARAM wParam, LPARAM)
{
    switch (msg)
    {
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_CLOSE)
            {
                PostMessageA(h, WM_CLOSE, 0, 0);
            }
            return 0;
        default:
            return 0;
    }
}

LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return proc(h, msg, wParam, lParam);
}

BOOL WINAPI PeekMessageA(MSG* pMsg, HWND, UINT, UINT, UINT flags)
{
    if (Queue().empty())
    {
        port_InjectTestInput(port_FrameCount());
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            port_OnInputEvent(e);
            Translate(e);
        }
        port_UpdatePad();
    }
    if (Queue().empty())
    {
        return FALSE;
    }
    *pMsg = Queue().front();
    if (flags & PM_REMOVE)
    {
        Queue().pop_front();
    }
    return TRUE;
}

BOOL WINAPI TranslateMessage(const MSG*)
{
    return TRUE;
}

LRESULT WINAPI DispatchMessageA(const MSG* pMsg)
{
    if (!pMsg->hwnd || W(pMsg->hwnd) != sMainWindow)
    {
        return 0;
    }
    return sMainWindow->mProc(pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
}

BOOL WINAPI PostMessageA(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Post(h, msg, wParam, lParam);
    return TRUE;
}

LRESULT WINAPI SendMessageA(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return h ? W(h)->mProc(h, msg, wParam, lParam) : 0;
}

void WINAPI PostQuitMessage(INT code)
{
    Post(NULL, WM_QUIT, code, 0);
}

BOOL WINAPI ShowWindow(HWND h, INT cmd)
{
    if (!h)
    {
        return FALSE;
    }
    SDL_Window* pSdl = W(h)->mSdl;
    switch (cmd)
    {
        case SW_HIDE:
            SDL_HideWindow(pSdl);
            break;
        case SW_MINIMIZE:
        case SW_SHOWMINIMIZED:
        case SW_SHOWMINNOACTIVE:
            SDL_MinimizeWindow(pSdl);
            break;
        default:
            SDL_ShowWindow(pSdl);
            break;
    }
    return TRUE;
}

BOOL WINAPI UpdateWindow(HWND)
{
    return TRUE;
}

BOOL WINAPI SetWindowPos(HWND h, HWND, INT x, INT y, INT cx, INT cy, UINT flags)
{
    if (!h)
    {
        return FALSE;
    }
    Window* pWin = W(h);
    if (!(flags & SWP_NOMOVE))
    {
        pWin->mX = x;
        pWin->mY = y;
    }
    if (!(flags & SWP_NOSIZE) && cx > 0 && cy > 0)
    {
        pWin->mW = cx;
        pWin->mH = cy;
    }
    return TRUE;
}

// No decorations are reported: the window rectangle is the client rectangle.
BOOL WINAPI GetClientRect(HWND h, RECT* pRect)
{
    if (!h)
    {
        return FALSE;
    }
    pRect->left = 0;
    pRect->top = 0;
    pRect->right = W(h)->mW;
    pRect->bottom = W(h)->mH;
    return TRUE;
}

BOOL WINAPI GetWindowRect(HWND h, RECT* pRect)
{
    if (!h)
    {
        return FALSE;
    }
    Window* pWin = W(h);
    pRect->left = pWin->mX;
    pRect->top = pWin->mY;
    pRect->right = pWin->mX + pWin->mW;
    pRect->bottom = pWin->mY + pWin->mH;
    return TRUE;
}

LONG WINAPI GetWindowLongA(HWND h, INT index)
{
    if (!h)
    {
        return 0;
    }
    Window* pWin = W(h);
    switch (index)
    {
        case GWL_STYLE:
            return pWin->mStyle;
        case GWL_EXSTYLE:
            return pWin->mExStyle;
        case GWLP_WNDPROC:
            return (LONG)(LONG_PTR)pWin->mProc;
        case GWLP_USERDATA:
            return (LONG)pWin->mUserData;
        default:
            return 0;
    }
}

LONG WINAPI SetWindowLongA(HWND h, INT index, LONG value)
{
    if (!h)
    {
        return 0;
    }
    Window* pWin = W(h);
    const LONG old = GetWindowLongA(h, index);
    switch (index)
    {
        case GWL_STYLE:
            pWin->mStyle = value;
            SDL_SetWindowBordered(pWin->mSdl, (value & WS_CAPTION) != 0);
            break;
        case GWL_EXSTYLE:
            pWin->mExStyle = value;
            break;
        case GWLP_WNDPROC:
            pWin->mProc = (WNDPROC)(LONG_PTR)value;
            break;
        case GWLP_USERDATA:
            pWin->mUserData = value;
            break;
        default:
            break;
    }
    return old;
}

INT WINAPI GetSystemMetrics(INT index)
{
    const SDL_DisplayMode* pMode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    switch (index)
    {
        case SM_CXSCREEN:
            return pMode ? pMode->w : 1920;
        case SM_CYSCREEN:
            return pMode ? pMode->h : 1080;
        default:
            return 0;
    }
}

BOOL WINAPI SetForegroundWindow(HWND h)
{
    if (h)
    {
        SDL_RaiseWindow(W(h)->mSdl);
    }
    return TRUE;
}

HWND WINAPI FindWindowExA(HWND, HWND, LPCSTR, LPCSTR)
{
    return NULL;
}

// The cursor follows the menus' mouse use (mouse.cpp), not the game's display mode.
INT WINAPI ShowCursor(BOOL show)
{
    sCursorCount += show ? 1 : -1;
    return sCursorCount;
}

HICON WINAPI LoadIconA(HINSTANCE, LPCSTR)
{
    return reinterpret_cast<HICON>(1);
}

HCURSOR WINAPI LoadCursorA(HINSTANCE, LPCSTR)
{
    return reinterpret_cast<HCURSOR>(1);
}

HDC WINAPI GetDC(HWND)
{
    return reinterpret_cast<HDC>(1);
}

INT WINAPI ReleaseDC(HWND, HDC)
{
    return 1;
}

// The game offers windowed mode only on a 16-bit desktop, which the renderer always emulates.
INT WINAPI GetDeviceCaps(HDC, INT index)
{
    return index == BITSPIXEL ? 16 : 0;
}

UINT_PTR WINAPI SetTimer(HWND, UINT_PTR id, UINT, TIMERPROC)
{
    return id ? id : 1;
}

BOOL WINAPI KillTimer(HWND, UINT_PTR)
{
    return TRUE;
}

BOOL WINAPI SetWindowTextA(HWND h, LPCSTR text)
{
    if (h)
    {
        W(h)->mTitle = text ? text : "";
        SDL_SetWindowTitle(W(h)->mSdl, W(h)->mTitle.c_str());
    }
    return TRUE;
}

INT WINAPI GetWindowTextA(HWND h, LPSTR out, INT size)
{
    if (!h || size <= 0)
    {
        return 0;
    }
    SDL_strlcpy(out, W(h)->mTitle.c_str(), size);
    return (INT)SDL_strlen(out);
}

INT WINAPI MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type)
{
    const SDL_MessageBoxFlags flags = (type & MB_ICONMASK) == MB_ICONERROR ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION;
    SDL_Log("%s: %s", caption ? caption : "", text ? text : "");
    if (!getenv("GTA2_NO_MESSAGEBOX"))
    {
        SDL_ShowSimpleMessageBox(flags, caption ? caption : "GTA2", text ? text : "", NULL);
    }
    return IDOK;
}

// The network lobby's dialogs: the port has no network play.
INT_PTR WINAPI DialogBoxParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM)
{
    return -1;
}

BOOL WINAPI EndDialog(HWND, INT_PTR)
{
    return TRUE;
}

BOOL WINAPI EnableWindow(HWND, BOOL)
{
    return FALSE;
}

HWND WINAPI GetDlgItem(HWND, INT)
{
    return NULL;
}

INT WINAPI GetDlgCtrlID(HWND)
{
    return 0;
}

UINT WINAPI GetDlgItemTextA(HWND, INT, LPSTR out, INT size)
{
    if (out && size > 0)
    {
        out[0] = 0;
    }
    return 0;
}

BOOL WINAPI SetDlgItemTextA(HWND, INT, LPCSTR)
{
    return FALSE;
}

LRESULT WINAPI SendDlgItemMessageA(HWND, INT, UINT, WPARAM, LPARAM)
{
    return 0;
}

HWND WINAPI GetParent(HWND)
{
    return NULL;
}

} // extern "C"
