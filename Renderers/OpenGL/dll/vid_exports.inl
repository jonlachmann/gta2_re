// The Vid_* exports that every video module on top of VidSystem answers the same way: glvideo.dll (WGL),
// Port/sdl_video.cpp (the port's SDL window) and Port/validation/nullvideo.cpp (no drawing). Included once by each,
// after vid_system.hpp and <windows.h>, with VID_EXPORTS_NAME set to its version string. The including file defines
// the ones that differ: Vid_Init_SYS, Vid_SetMode and Vid_FlipBuffers.
#define EXPORT extern "C" __declspec(dllexport)

static HINSTANCE gDllInstance;
static void* gVidFuncs;

static VidSystem* Sys(SVideo* pVideo)
{
    return VidSystem::FromVideo(pVideo);
}

EXPORT SVidVersion* __stdcall Vid_GetVersion()
{
    static SVidVersion version = {0x3FE7AE14, VID_EXPORTS_NAME};
    return &version;
}

EXPORT s32 __stdcall Vid_InitDLL(HINSTANCE hInstance, void* pFuncs)
{
    gDllInstance = hInstance;
    gVidFuncs = pFuncs;
    return 0;
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

// The game calls both pairs; they do the same
EXPORT s32 __stdcall Vid_GetSurface(SVideo* pVideo)
{
    return Vid_EnableWrites(pVideo);
}

EXPORT s32 __stdcall Vid_FreeSurface(SVideo* pVideo)
{
    return Vid_DisableWrites(pVideo);
}

// Without a GL context (headless) the renderer ignores the clear
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
