// The video DLL's Vid_* API on top of VidSystem, with an OpenGL ES context on the game's SDL window,
// and the registration of the built-in render and video DLLs.
#include "port_platform.hpp"
#include "../../renderer/gl_context.hpp"
#include "../../renderer/vid_system.hpp"
#include <stdlib.h>

#define VID_EXPORT extern "C"

namespace
{
VidSystem* Sys(SVideo* pVideo)
{
    return VidSystem::FromVideo(pVideo);
}

void* sVidFuncs;
unsigned sFrameCount;

// Scroll Lock toggles faithful rendering and the gta2gl.ini options.
void PollToggleKey(VidSystem* pSys)
{
    static bool sWasDown;
    static const bool sDisabled = getenv("GTA2GL_NO_TOGGLE_KEY") != NULL;
    if (sDisabled)
    {
        return;
    }
    const bool down = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_SCROLLLOCK];
    if (down && !sWasDown)
    {
        pSys->ToggleFaithful();
    }
    sWasDown = down;
}
} // namespace

unsigned port_FrameCount()
{
    return sFrameCount;
}

VID_EXPORT SVidVersion* GTA2_CC Vid_GetVersion()
{
    static SVidVersion version = {0x3FE7AE14, "OpenGL ES 3.0 video"};
    return &version;
}

VID_EXPORT s32 GTA2_CC Vid_InitDLL(void*, void* pFuncs)
{
    sVidFuncs = pFuncs;
    return 0;
}

VID_EXPORT SVideo* GTA2_CC Vid_Init_SYS(void* hInstance, u16 flags)
{
    VidSystem* pSys = new VidSystem();
    pSys->Init(hInstance, flags, sVidFuncs, "gta2gl.ini");
    return pSys->Video();
}

VID_EXPORT s32 GTA2_CC Vid_CheckMode(SVideo* pVideo, s32 width, s32 height, s32 bpp)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->CheckMode(width, height, bpp) : 0;
}

VID_EXPORT SDisplayMode* GTA2_CC Vid_FindMode(SVideo* pVideo, s32 modeId)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->FindMode(modeId) : 0;
}

VID_EXPORT s32 GTA2_CC Vid_FindFirstMode(SVideo* pVideo, s32 bpp)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->FindFirstMode(bpp) : 0;
}

VID_EXPORT s32 GTA2_CC Vid_FindNextMode(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->FindNextMode() : 0;
}

VID_EXPORT SDevice* GTA2_CC Vid_FindDevice(SVideo* pVideo, s32 deviceId)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->FindDevice(deviceId) : 0;
}

VID_EXPORT s32 GTA2_CC Vid_SetDevice(SVideo*, s32)
{
    return 0;
}

VID_EXPORT void GTA2_CC Vid_CloseScreen(SVideo*)
{
}

// Returns 0 on success (the game treats 1 as failure for windowed mode, non zero for full screen).
// Full screen is a borderless desktop window; the frame is scaled with letterboxing on present.
VID_EXPORT s32 GTA2_CC Vid_SetMode(SVideo* pVideo, HWND hWnd, s32 modeId)
{
    VidSystem* pSys = Sys(pVideo);
    SDL_Window* pWindow = port_GetSDLWindow(hWnd);
    if (!pSys || !pWindow)
    {
        return 1;
    }
    s32 w;
    s32 h;
    if (modeId == VidSystem::kWindowedModeId)
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        w = rc.right - rc.left;
        h = rc.bottom - rc.top;
        port_SetFullscreen(hWnd, false);
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
        port_SetFullscreen(hWnd, true);
    }
    if (w <= 0 || h <= 0)
    {
        return 1;
    }
    pVideo->field_4C0_hwnd = hWnd;
    if (!pSys->HasContext())
    {
        const char* pError = "";
        IGLContext* pCtx = CreateGLContext(pWindow, &pError);
        if (!pCtx || !pSys->AttachContext(pCtx, &pError))
        {
            SDL_Log("OpenGL ES 3.0 initialisation failed: %s", pError);
            if (!getenv("GTA2_NO_MESSAGEBOX"))
            {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenGL ES 3.0 initialisation failed", pError, pWindow);
            }
            return 1;
        }
    }
    pSys->SetMode(modeId, w, h);
    return 0;
}

VID_EXPORT void GTA2_CC Vid_FlipBuffers(SVideo* pVideo)
{
    sFrameCount++;
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        PollToggleKey(pSys);
        pSys->Flip();
    }
}

VID_EXPORT void GTA2_CC Vid_ReleaseSurface(SVideo*)
{
}

VID_EXPORT void GTA2_CC Vid_GrabSurface(SVideo*)
{
}

VID_EXPORT void GTA2_CC Vid_ShutDown_SYS(SVideo* pVideo)
{
    delete Sys(pVideo);
}

VID_EXPORT s32 GTA2_CC Vid_EnableWrites(SVideo* pVideo)
{
    if (VidSystem* pSys = Sys(pVideo))
    {
        pSys->GetSurface();
    }
    return 0;
}

VID_EXPORT s32 GTA2_CC Vid_DisableWrites(SVideo* pVideo)
{
    if (VidSystem* pSys = Sys(pVideo))
    {
        pSys->FreeSurface();
    }
    return 0;
}

VID_EXPORT s32 GTA2_CC Vid_GetSurface(SVideo* pVideo)
{
    if (VidSystem* pSys = Sys(pVideo))
    {
        pSys->GetSurface();
    }
    return 0;
}

VID_EXPORT s32 GTA2_CC Vid_FreeSurface(SVideo* pVideo)
{
    if (VidSystem* pSys = Sys(pVideo))
    {
        pSys->FreeSurface();
    }
    return 0;
}

VID_EXPORT s32 GTA2_CC Vid_ClearScreen(SVideo* pVideo, u8 r, u8 g, u8 b, s32 left, s32 top, s32 right, s32 bottom)
{
    if (VidSystem* pSys = Sys(pVideo))
    {
        pSys->Clear(r, g, b, left, top, right, bottom);
    }
    return 0;
}

VID_EXPORT s32 GTA2_CC Vid_SetGamma(SVideo* pVideo, f32 r, f32 g, f32 b)
{
    VidSystem* pSys = Sys(pVideo);
    return pSys ? pSys->SetGamma(r, g, b) : 0;
}

VID_EXPORT s32 GTA2_CC Vid_WindowProc(SVideo*, HWND, DWORD, WPARAM, LPARAM)
{
    return 0;
}

namespace
{
#define VID_ENTRY(name) {#name, reinterpret_cast<void*>(&name)}
const PortExport kVidExports[] = {
    VID_ENTRY(Vid_GetVersion),
    VID_ENTRY(Vid_InitDLL),
    VID_ENTRY(Vid_Init_SYS),
    VID_ENTRY(Vid_CheckMode),
    VID_ENTRY(Vid_FindMode),
    VID_ENTRY(Vid_FindFirstMode),
    VID_ENTRY(Vid_FindNextMode),
    VID_ENTRY(Vid_FindDevice),
    VID_ENTRY(Vid_SetDevice),
    VID_ENTRY(Vid_CloseScreen),
    VID_ENTRY(Vid_SetMode),
    VID_ENTRY(Vid_FlipBuffers),
    VID_ENTRY(Vid_ReleaseSurface),
    VID_ENTRY(Vid_GrabSurface),
    VID_ENTRY(Vid_ShutDown_SYS),
    VID_ENTRY(Vid_EnableWrites),
    VID_ENTRY(Vid_DisableWrites),
    VID_ENTRY(Vid_GetSurface),
    VID_ENTRY(Vid_FreeSurface),
    VID_ENTRY(Vid_ClearScreen),
    VID_ENTRY(Vid_SetGamma),
    VID_ENTRY(Vid_WindowProc),
};
} // namespace

// Whatever the registry names (DMA's DLLs or the old GL ones), the built-in renderer answers.
void port_RegisterRenderer()
{
    const int vidCount = sizeof(kVidExports) / sizeof(kVidExports[0]);
    for (const char* name : {"dmavideo.dll", "glvideo.dll"})
    {
        port_RegisterDll(name, kVidExports, vidCount);
    }
    for (const char* name : {"d3ddll.dll", "glrender.dll"})
    {
        port_RegisterDll(name, kGbhExports, kGbhExportCount);
    }
}
