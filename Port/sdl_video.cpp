// Port (docs/port.md): the Vid_* functions of Renderers/OpenGL/dll/glvideo.cpp on the SDL window of
// sdl_win32.cpp, with an SDL GL context instead of WGL (the ones that don't touch the window are shared,
// vid_exports.inl). Exported, so the game's DMA_Video_LoadDll_5EB970 finds them in the exe itself.
#include "Renderers/OpenGL/core/vid_system.hpp"
#include "Renderers/OpenGL/platform/gl_context.hpp"
#include "sdl_win32.hpp"
#include "headless/sim_bench.hpp"
#include "headless/state_log.hpp"
#include <SDL3/SDL.h>
#include <stdio.h>

#define VID_EXPORTS_NAME "OpenGL 3.3 video (SDL3)"
#include "Renderers/OpenGL/core/vid_exports.inl"

static unsigned gFlips;

EXPORT SVideo* __stdcall Vid_Init_SYS(HINSTANCE hInstance, u16 flags)
{
    StateLog_Init();
    VidSystem* pSys = new VidSystem();
    pSys->Init(hInstance, flags, gVidFuncs, "gta2gl.ini");
    return pSys->Video();
}

// Returns 0 on success (the game treats 1 as failure for windowed mode, non zero for full screen).
EXPORT s32 __stdcall Vid_SetMode(SVideo* pVideo, HWND hWnd, s32 modeId)
{
    VidSystem* pSys = Sys(pVideo);
    SDL_Window* pWindow = (SDL_Window*)Port_GetSDLWindow();
    if (!pSys || !pWindow)
    {
        return 1;
    }
    s32 w = 640, h = 480;
    if (modeId == VidSystem::kWindowedModeId)
    {
        SDL_SetWindowFullscreen(pWindow, false);
        // The game's resolution, not the window's size: after the window was resized, a mode reset (on getting focus
        // back) made the frame the window's size while the game still drew 640x480 in a corner of it
        if (!Port_GetRequestedWindowSize(&w, &h))
        {
            SDL_GetWindowSize(pWindow, &w, &h);
        }
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
        // Borderless full screen on the desktop's mode: the frame is scaled with letterboxing on present.
        SDL_SetWindowFullscreen(pWindow, true);
    }
    if (w <= 0 || h <= 0)
    {
        return 1;
    }
    pVideo->field_4C0_hwnd = hWnd;
    if (!pSys->HasContext() && !Port_IsHeadless())
    {
        const char* pError = "";
        IGLContext* pCtx = CreateGLContextForSDLWindow(pWindow, &pError);
        if (!pCtx || !pSys->AttachContext(pCtx, &pError))
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenGL 3.3 initialisation failed", pError, pWindow);
            return 1;
        }
    }
    pSys->SetMode(modeId, w, h);
    return 0;
}

EXPORT void __stdcall Vid_FlipBuffers(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        pSys->Flip(); // returns at once without a GL context (headless)
    }
    StateLog_Frame(++gFlips);
    SimBench_Frame(gFlips);
}

