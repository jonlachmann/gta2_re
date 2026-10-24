// glvideo.dll: Vid_* exports on top of VidSystem, with a WGL context on the game's window.
#include "../core/vid_system.hpp"
#include "../platform/gl_context.hpp"
#include "dinput_hook.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define VID_EXPORTS_NAME "OpenGL 3.3 video"
#include "../core/vid_exports.inl"

static void Log(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

EXPORT SVideo* __stdcall Vid_Init_SYS(HINSTANCE hInstance, u16 flags)
{
    // gta2gl.ini lives next to this DLL.
    char iniPath[MAX_PATH] = "gta2gl.ini";
    if (gDllInstance && GetModuleFileNameA(gDllInstance, iniPath, MAX_PATH))
    {
        char* slash = strrchr(iniPath, '\\');
        strcpy(slash ? slash + 1 : iniPath, "gta2gl.ini");
    }
    VidSystem* pSys = new VidSystem();
    pSys->Init(gDllInstance ? gDllInstance : hInstance, flags, gVidFuncs, iniPath);
    Log("glvideo: Vid_Init_SYS flags=%x\n", flags);
    InstallDInputHook(pSys->FrameCounter());
    return pSys->Video();
}

// Returns 0 on success (the game treats 1 as failure for windowed mode, non zero for full screen).
EXPORT s32 __stdcall Vid_SetMode(SVideo* pVideo, HWND hWnd, s32 modeId)
{
    VidSystem* pSys = Sys(pVideo);
    if (!pSys || !hWnd)
    {
        return 1;
    }
    s32 w = 640, h = 480;
    if (modeId == VidSystem::kWindowedModeId)
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        w = rc.right - rc.left;
        h = rc.bottom - rc.top;
    }
    else
    {
        SDisplayMode* pMode = pSys->FindMode(modeId);
        if (!pMode)
        {
            return 1;
        }
        w = pMode->field_8_width;
        h = pMode->field_C_height;
        // Borderless full screen: the window covers the monitor, the frame is scaled on present.
        SetWindowPos(hWnd, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_NOACTIVATE);
    }
    if (w <= 0 || h <= 0)
    {
        return 1;
    }
    pVideo->field_4C0_hwnd = hWnd;
    if (!pSys->HasContext())
    {
        const char* pError = "";
        IGLContext* pCtx = CreateGLContextFromNativeWindow(hWnd, &pError);
        if (!pCtx || !pSys->AttachContext(pCtx, &pError))
        {
            Log("glvideo: GL init failed: %s\n", pError);
            MessageBoxA(hWnd, pError, "glvideo: OpenGL 3.3 initialisation failed", MB_OK | MB_ICONERROR);
            return 1;
        }
    }
    pSys->SetMode(modeId, w, h);
    Log("glvideo: Vid_SetMode mode=%d %dx%d\n", modeId, w, h);
    return 0;
}

// Enhancement: F12 switches between the faithful renderer and the gta2gl.ini options. Not a lock key (wine
// resyncs those), and the game only reads it with the do_debug_keys debug setting.
static void PollToggleKey(VidSystem* pSys, HWND hWnd)
{
    static bool sWasDown;
    const bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0 && GetForegroundWindow() == hWnd;
    if (down && !sWasDown)
    {
        pSys->ToggleFaithful();
    }
    sWasDown = down;
}

EXPORT void __stdcall Vid_FlipBuffers(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        PollToggleKey(pSys, (HWND)pVideo->field_4C0_hwnd);
        pSys->Flip();
    }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
