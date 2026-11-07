// Port (docs/port.md): nullvideo.dll, a video DLL for 10.5.exe that draws nothing, to compare it with
// gta2_port.exe quickly: register it as videoname with rendername glrender.dll (PR #837's gbh_* layer, whose
// renderer here never gets a GL context, so it draws nothing either). It runs GTA2_SPEED (fast_clock.cpp)
// and GTA2_STATELOG (state_log.cpp, with GTA2_STATELOG_TABLE from a port run) inside 10.5.exe.
#include "../../Renderers/OpenGL/core/vid_system.hpp"
#include "fast_clock.hpp"
#include "state_log.hpp"
#include <windows.h>

#define EXPORT extern "C" __declspec(dllexport)

static void* gVidFuncs;
static unsigned gFlips;

static VidSystem* Sys(SVideo* pVideo)
{
    return VidSystem::FromVideo(pVideo);
}

EXPORT SVidVersion* __stdcall Vid_GetVersion()
{
    static SVidVersion version = {0x3FE7AE14, "headless video"};
    return &version;
}

EXPORT s32 __stdcall Vid_InitDLL(HINSTANCE, void* pFuncs)
{
    gVidFuncs = pFuncs;
    return 0;
}

EXPORT SVideo* __stdcall Vid_Init_SYS(HINSTANCE hInstance, u16 flags)
{
    FastClock_Install();
    StateLog_Init();
    VidSystem* pSys = new VidSystem();
    pSys->Init(hInstance, flags, gVidFuncs, "gta2gl.ini");
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

// No GL context: the mode only sets the logical size the game and the gbh_* layer work with.
EXPORT s32 __stdcall Vid_SetMode(SVideo* pVideo, HWND hWnd, s32 modeId)
{
    VidSystem* pSys = Sys(pVideo);
    if (!pSys)
    {
        return 1;
    }
    s32 w = 640, h = 480;
    if (modeId != VidSystem::kWindowedModeId)
    {
        SDisplayMode* pMode = pSys->FindMode(modeId);
        if (!pMode)
        {
            return 1;
        }
        w = pMode->field_8_width;
        h = pMode->field_C_height;
    }
    else if (hWnd)
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (rc.right > rc.left && rc.bottom > rc.top)
        {
            w = rc.right - rc.left;
            h = rc.bottom - rc.top;
        }
    }
    pVideo->field_4C0_hwnd = hWnd;
    pSys->SetMode(modeId, w, h);
    return 0;
}

EXPORT void __stdcall Vid_FlipBuffers(SVideo*)
{
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
    return Vid_EnableWrites(pVideo);
}

EXPORT s32 __stdcall Vid_FreeSurface(SVideo* pVideo)
{
    return Vid_DisableWrites(pVideo);
}

EXPORT s32 __stdcall Vid_ClearScreen(SVideo*, u8, u8, u8, s32, s32, s32, s32)
{
    return 0;
}

EXPORT s32 __stdcall Vid_SetGamma(SVideo*, f32, f32, f32)
{
    return 0;
}

EXPORT s32 __stdcall Vid_WindowProc(SVideo*, HWND, DWORD, WPARAM, LPARAM)
{
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
