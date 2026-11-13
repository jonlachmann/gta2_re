// The video DLL's Vid_* API on top of VidSystem, with an OpenGL ES context on the game's SDL window,
// and the registration of the built-in render and video DLLs.
#include "port_platform.hpp"
#include "../../net/eval/sim_bench.hpp"
#include "../../renderer/gl_context.hpp"
#include "../../renderer/vid_system.hpp"
#include <stdlib.h>
#include <vector>

#define VID_EXPORT extern "C"

namespace
{
VidSystem* Sys(SVideo* pVideo)
{
    return VidSystem::FromVideo(pVideo);
}

void* sVidFuncs;
VidSystem* sVidSystem;
// The options last set from the game's menu, for a renderer made later.
RenderOptions sGameOptions;
bool sHasGameOptions;
// Test frames: flips, except the game's own frames, for which logic steps count instead.
unsigned sTestFrame;
bool sGameFlip;

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

void port_SetRenderOptions(bool trueColour, int bloomTenths, bool perPixelLighting, int textureFilter, int msaa, bool softShadows)
{
    sGameOptions = RenderOptions();
    sGameOptions.mTrueColour = trueColour;
    sGameOptions.mBloom = bloomTenths / 10.0f;
    sGameOptions.mPerPixelLighting = perPixelLighting;
    sGameOptions.mTextureFilter = textureFilter;
    sGameOptions.mMsaa = msaa;
    sGameOptions.mSoftShadows = softShadows;
    sHasGameOptions = true;
    if (sVidSystem)
    {
        sVidSystem->SetConfiguredOptions(sGameOptions);
    }
}

// As the renderer draws them: off with Scroll Lock's faithful rendering.
bool port_SoftShadows()
{
    return sVidSystem && sVidSystem->Renderer().Options().mSoftShadows;
}

// 8 until the renderer has a context.
int port_MaxMsaaSamples()
{
    return sVidSystem && sVidSystem->Renderer().MaxSamples() > 0 ? sVidSystem->Renderer().MaxSamples() : 8;
}

unsigned port_FrameCount()
{
    return sTestFrame;
}

void port_CountGameStep()
{
    sTestFrame++;
}

void port_SetGameFlip(bool gameFlip)
{
    sGameFlip = gameFlip;
}

unsigned long long port_NowNs()
{
    return (unsigned long long)(SDL_GetTicksNS() * port_ClockSpeed());
}

void port_DelayNs(unsigned long long ns)
{
    SDL_DelayPrecise((Uint64)(ns / port_ClockSpeed()));
}

// The refresh period the swap waits for; 0 without vsync or when the screen does not say.
unsigned long long port_SwapRefreshNs()
{
    SDL_Window* pWindow = port_MainSDLWindow();
    int interval = 0;
    if (!sVidSystem || !pWindow || !SDL_GL_GetSwapInterval(&interval) || !interval)
    {
        return 0;
    }
    const SDL_DisplayMode* pMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(pWindow));
    return pMode && pMode->refresh_rate > 0.0f ? static_cast<unsigned long long>(1e9 / pMode->refresh_rate + 0.5) : 0;
}

void port_PresentAgain()
{
    if (sVidSystem)
    {
        sVidSystem->Represent();
    }
}

void port_FrameSize(int* pW, int* pH)
{
    if (sVidSystem)
    {
        *pW = sVidSystem->Renderer().LogicalWidth();
        *pH = sVidSystem->Renderer().LogicalHeight();
    }
}

namespace
{
DWORD ScreenGet(const char* name, DWORD def)
{
    HKEY hKey;
    DWORD value = def;
    DWORD size = sizeof(value);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Screen", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExA(hKey, name, NULL, NULL, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS)
        {
            value = def;
        }
        RegCloseKey(hKey);
    }
    return value;
}

void ScreenSet(const char* name, DWORD value)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Screen", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExA(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(hKey);
    }
}

// The full screen modes: 0 the desktop's (borderless), then the screen's own sizes and refresh rates, one
// per rate in whole hertz.
struct FullscreenMode
{
    SDL_DisplayMode mMode;
    int mCentiHz;
};

int CentiHz(const SDL_DisplayMode& mode)
{
    return (int)(mode.refresh_rate * 100.0f + 0.5f);
}

std::vector<FullscreenMode> FullscreenModes()
{
    std::vector<FullscreenMode> modes(1);
    modes[0].mMode = SDL_DisplayMode();
    modes[0].mCentiHz = 0;
    if (const SDL_DisplayMode* pDesktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay()))
    {
        modes[0].mMode = *pDesktop;
        modes[0].mCentiHz = CentiHz(*pDesktop);
    }
    int count = 0;
    SDL_DisplayMode** ppModes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &count);
    for (int i = 0; ppModes && i < count; i++)
    {
        const FullscreenMode mode = {*ppModes[i], CentiHz(*ppModes[i])};
        bool known = false;
        for (size_t j = 1; j < modes.size(); j++)
        {
            known = known || (modes[j].mMode.w == mode.mMode.w && modes[j].mMode.h == mode.mMode.h && (modes[j].mCentiHz + 50) / 100 == (mode.mCentiHz + 50) / 100);
        }
        if (!known)
        {
            modes.push_back(mode);
        }
    }
    SDL_free(ppModes);
    return modes;
}

// The game draws at the screen's resolution: its view keeps the original's 4:3 height and widens
// with the screen (Camera::UpdateBoundaries), its HUD follows (Camera::SetScreenSize).
void GameSize(int screenW, int screenH, int* pW, int* pH)
{
    *pW = screenW < 640 ? 640 : screenW & ~1;
    *pH = screenH < 480 ? 480 : screenH;
}
} // namespace

void port_GetWindowSize(int* pW, int* pH)
{
    const SDL_DisplayMode* pMode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    const int desktopW = pMode ? pMode->w : 1920;
    const int desktopH = pMode ? pMode->h : 1080;
    *pW = (int)ScreenGet("port_window_width", desktopW * 2 / 3);
    *pH = (int)ScreenGet("port_window_height", desktopH * 2 / 3);
}

void port_SaveWindowSize(int w, int h)
{
    ScreenSet("port_window_width", w);
    ScreenSet("port_window_height", h);
}

int port_FullscreenModeCount()
{
    return (int)FullscreenModes().size();
}

int port_FullscreenModeIndex()
{
    const int w = (int)ScreenGet("port_fullscreen_width", 0);
    const int h = (int)ScreenGet("port_fullscreen_height", 0);
    const int centiHz = (int)ScreenGet("port_fullscreen_centihz", 0);
    const std::vector<FullscreenMode> modes = FullscreenModes();
    for (size_t i = 1; i < modes.size(); i++)
    {
        if (modes[i].mMode.w == w && modes[i].mMode.h == h && modes[i].mCentiHz == centiHz)
        {
            return (int)i;
        }
    }
    return 0;
}

void port_FullscreenModeSize(int index, int* pW, int* pH, int* pCentiHz)
{
    const std::vector<FullscreenMode> modes = FullscreenModes();
    const FullscreenMode& mode = modes[index > 0 && index < (int)modes.size() ? index : 0];
    *pW = mode.mMode.w;
    *pH = mode.mMode.h;
    *pCentiHz = mode.mCentiHz;
}

void port_SetFullscreenModeIndex(int index)
{
    int w = 0;
    int h = 0;
    int centiHz = 0;
    if (index > 0)
    {
        port_FullscreenModeSize(index, &w, &h, &centiHz);
    }
    ScreenSet("port_fullscreen_width", w);
    ScreenSet("port_fullscreen_height", h);
    ScreenSet("port_fullscreen_centihz", centiHz);
    port_UpdateGameResolution();
}

const SDL_DisplayMode* port_FullscreenMode()
{
    static SDL_DisplayMode sMode;
    const int index = port_FullscreenModeIndex();
    if (!index)
    {
        return NULL;
    }
    sMode = FullscreenModes()[index].mMode;
    return &sMode;
}

// VSYNC: adaptive where the driver has it (a late frame is shown at once), else plain.
void port_ApplyVsync()
{
    if (!SDL_GL_GetCurrentContext())
    {
        return;
    }
    if (!ScreenGet("port_vsync", 1))
    {
        SDL_GL_SetSwapInterval(0);
    }
    else if (!SDL_GL_SetSwapInterval(-1))
    {
        SDL_GL_SetSwapInterval(1);
    }
}

void port_UpdateGameResolution()
{
    int w;
    int h;
    const SDL_DisplayMode* pMode = port_FullscreenMode();
    if (!pMode)
    {
        pMode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    }
    if (pMode)
    {
        GameSize(pMode->w, pMode->h, &w, &h);
        ScreenSet("full_width", w);
        ScreenSet("full_height", h);
    }
    int windowW;
    int windowH;
    port_GetWindowSize(&windowW, &windowH);
    GameSize(windowW, windowH, &w, &h);
    ScreenSet("window_width", w);
    ScreenSet("window_height", h);
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
    sVidSystem = pSys;
    if (sHasGameOptions)
    {
        pSys->SetConfiguredOptions(sGameOptions);
    }
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
    if (!pSys->HasContext() && !port_Headless())
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
        port_ApplyVsync();
    }
    pSys->SetMode(modeId, w, h);
    return 0;
}

VID_EXPORT void GTA2_CC Vid_FlipBuffers(SVideo* pVideo)
{
    if (!sGameFlip)
    {
        sTestFrame++;
    }
    VidSystem* pSys = Sys(pVideo);
    if (pSys)
    {
        pSys->SetTestFrame(sTestFrame);
        pSys->Renderer().SetSmoothMagnify(port_FrontendActive());
        PollToggleKey(pSys);
        // In game nothing writes the CPU surface (movies play outside games); scanning it is costly on the 3DS.
        pSys->Flip(!port_GameRunning());
    }
    port_MouseEndFrame();
    static unsigned sFlips;
    SimBench_Frame(++sFlips); // the network game's simulation bench (GTA2_SIMBENCH, port/docs/netcode.md)
}

VID_EXPORT void GTA2_CC Vid_ReleaseSurface(SVideo*)
{
}

VID_EXPORT void GTA2_CC Vid_GrabSurface(SVideo*)
{
}

VID_EXPORT void GTA2_CC Vid_ShutDown_SYS(SVideo* pVideo)
{
    VidSystem* pSys = Sys(pVideo);
    if (pSys == sVidSystem)
    {
        sVidSystem = NULL;
    }
    delete pSys;
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
