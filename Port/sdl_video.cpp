// Port (docs/port.md): the Vid_* functions of Renderers/OpenGL/dll/glvideo.cpp on the SDL window of
// sdl_win32.cpp, with an SDL GL context instead of WGL. Exported, so the game's DMA_Video_LoadDll_5EB970
// finds them in the exe itself.
#include "Renderers/OpenGL/core/vid_system.hpp"
#include "Renderers/OpenGL/platform/gl_context.hpp"
#include "sdl_win32.hpp"
#include "headless/state_log.hpp"
#include <SDL3/SDL.h>
#include <stdio.h>

#define EXPORT extern "C" __declspec(dllexport)

static void* gVidFuncs;
static unsigned gFlips;

// GTA2_HEADLESS=1: no GL context and no drawing; the gbh_* layer still runs (docs/port.md, M2)
static bool Headless()
{
    return SDL_getenv("GTA2_HEADLESS") != NULL;
}

static VidSystem* Sys(SVideo* pVideo)
{
    return VidSystem::FromVideo(pVideo);
}

EXPORT SVidVersion* __stdcall Vid_GetVersion()
{
    static SVidVersion version = {0x3FE7AE14, "OpenGL 3.3 video (SDL3)"};
    return &version;
}

EXPORT s32 __stdcall Vid_InitDLL(HINSTANCE, void* pFuncs)
{
    gVidFuncs = pFuncs;
    return 0;
}

EXPORT SVideo* __stdcall Vid_Init_SYS(HINSTANCE hInstance, u16 flags)
{
    VidSystem* pSys = new VidSystem();
    pSys->Init(hInstance, flags, gVidFuncs, "gta2gl.ini");
    StateLog_Init();
    return pSys->Video();
}

EXPORT s32 __stdcall Vid_CheckMode(SVideo* pVideo, s32 width, s32 height, s32 bpp)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->CheckMode(width, height, bpp) : 0;
}

EXPORT SDisplayMode* __stdcall Vid_FindMode(SVideo* pVideo, s32 modeId)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->FindMode(modeId) : 0;
}

EXPORT s32 __stdcall Vid_FindFirstMode(SVideo* pVideo, s32 bpp)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->FindFirstMode(bpp) : 0;
}

EXPORT s32 __stdcall Vid_FindNextMode(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->FindNextMode() : 0;
}

EXPORT SDevice* __stdcall Vid_FindDevice(SVideo* pVideo, s32 deviceId)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->FindDevice(deviceId) : 0;
}

EXPORT s32 __stdcall Vid_SetDevice(SVideo*, s32)
{
    return 0;
}

EXPORT void __stdcall Vid_CloseScreen(SVideo*)
{
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
        SDL_GetWindowSize(pWindow, &w, &h);
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
    if (!pSys->HasContext() && !Headless())
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
}

EXPORT void __stdcall Vid_ReleaseSurface(SVideo*)
{
}

EXPORT void __stdcall Vid_GrabSurface(SVideo*)
{
}

EXPORT void __stdcall Vid_ShutDown_SYS(SVideo* pVideo)
{
    delete Sys(pVideo);
}

EXPORT s32 __stdcall Vid_EnableWrites(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        pSys->GetSurface();
    }
    return 0;
}

EXPORT s32 __stdcall Vid_DisableWrites(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        pSys->FreeSurface();
    }
    return 0;
}

EXPORT s32 __stdcall Vid_GetSurface(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        pSys->GetSurface();
    }
    return 0;
}

EXPORT s32 __stdcall Vid_FreeSurface(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        pSys->FreeSurface();
    }
    return 0;
}

EXPORT s32 __stdcall Vid_ClearScreen(SVideo* pVideo, u8 r, u8 g, u8 b, s32 left, s32 top, s32 right, s32 bottom)
{
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        pSys->Clear(r, g, b, left, top, right, bottom);
    }
    return 0;
}

EXPORT s32 __stdcall Vid_SetGamma(SVideo* pVideo, f32 r, f32 g, f32 b)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->SetGamma(r, g, b) : 0;
}

EXPORT s32 __stdcall Vid_WindowProc(SVideo*, HWND, DWORD, WPARAM, LPARAM)
{
    return 0;
}

