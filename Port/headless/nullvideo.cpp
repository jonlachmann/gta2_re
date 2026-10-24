// Port (docs/port.md): nullvideo.dll, a video DLL for 10.5.exe that draws nothing, to compare it with
// gta2_port.exe quickly: register it as videoname with rendername glrender.dll (PR #837's gbh_* layer, whose
// renderer here never gets a GL context, so it draws nothing either). It runs GTA2_SPEED (fast_clock.cpp)
// and GTA2_STATELOG (state_log.cpp, with GTA2_STATELOG_TABLE from a port run) inside 10.5.exe.
#include "../../Renderers/OpenGL/core/vid_system.hpp"
#include "fast_clock.hpp"
#include "state_log.hpp"
#include <windows.h>

#define VID_EXPORTS_NAME "headless video"
#include "../../Renderers/OpenGL/core/vid_exports.inl"

static unsigned gFlips;

EXPORT SVideo* __stdcall Vid_Init_SYS(HINSTANCE hInstance, u16 flags)
{
    FastClock_Install();
    StateLog_Init();
    VidSystem* pSys = new VidSystem();
    pSys->Init(hInstance, flags, gVidFuncs, "gta2gl.ini");
    return pSys->Video();
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

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
