#pragma once

#include "types.hpp"
#include <ddraw.h>
#include <set>
#include <vector>
#include <windows.h>

struct SVideoFunctions;

struct SVideo
{
    DWORD field_0;
    DWORD field_4_flags;
    DWORD field_8_found_width;
    DWORD field_C_found_height;
    DWORD field_10_found_rgb_bit_count;
    DWORD field_14_display_mode_count_2_q;
    DWORD field_18_num_guids;
    DWORD field_1C_num_display_modes;
    DWORD field_20_num_enums;
    struct SDisplayMode* field_24_head_ptr;
    struct SDisplayMode* field_28_display_mode_array;
    struct SDevice* field_2C_device_info_head_ptr;
    struct SDevice* field_30_device_info_last;
    DWORD field_34_active_device_id;
    DWORD field_38;
    struct SDisplayMode* field_3C_current_enum_ptr;
    DWORD field_40_full_screen;
    DWORD field_44;
    DWORD field_48_rect_right;
    DWORD field_4C_rect_bottom;
    LPVOID field_50_surface_pixels_ptr;
    DWORD field_54_surface_pixels_pitch;
    DWORD field_58;
    DWORD field_5C;
    DWORD field_60_g;
    DWORD field_64_r;
    DWORD field_68_b;
    DWORD field_6C;
    DWORD field_70;
    DWORD field_74;
    DWORD field_78;
    HINSTANCE field_7C_self_dll_handle;
    DWORD field_80_active_mode_q;
    SVideoFunctions* field_84_from_initDLL;
    DWORD field_88_last_error;
    IDirectDraw7* field_8C_DirectDraw7;
    char buffer[144];
    IDirectDraw4* field_120_IDDraw4;
    GUID field_124;
    class IDirectDrawSurface7Stub* field_134_SurfacePrimary; // passed to bink
    IDirectDrawSurface7Stub* field_138_Surface;
    DDSURFACEDESC2 field_13C_DDSurfaceDesc7;
    LPDIRECTDRAWCLIPPER field_1B8_clipper;
    DWORD field_1BC;
    DWORD field_1C0;
    DWORD field_1C4;
    DDCAPS field_1C8_device_caps;
    DDCAPS field_344_hel_caps;
    HWND field_4C0_hwnd;
};

static_assert(sizeof(DDCAPS) == 0x17C, "Wrong sized DDCAPS");
static_assert(sizeof(void*) != 4 || sizeof(DDSURFACEDESC2) == 0x7C, "Wrong sized DDSURFACEDESC2");
static_assert(sizeof(void*) != 4 || sizeof(SVideo) == 0x4C4, "Wrong sized SVideo");

struct SDevice
{
    DWORD field_0_id;
    DWORD field_4_flags; // must have 0x1 set to avoid window min on WM_KILLFOCUS
    char* field_8_driver_name;
    char* field_C_driver_desc;
    SDevice* field_10_next_ptr;
    GUID* field_14_pDeviceGuid;
    GUID field_18_guid;
    DWORD field_28_dwVidMemTotal;
    DWORD field_2C_end_buffer; // TODO: Actually a word and SDevice is 0x2E ?
};
static_assert(sizeof(void*) != 4 || sizeof(SDevice) == 0x30, "Wrong sized SDevice");

struct SDisplayMode
{
    DWORD field_0_display_mode_idx;
    DWORD field_4_deviceId;
    DWORD field_8_width;
    DWORD field_C_height;
    DWORD field_10_pitch;
    DWORD field_14_rgb_bit_count;
    DWORD field_18;
    DWORD field_1C;
    DWORD field_20;
    DWORD field_24;
    DWORD field_28;
    DWORD field_2C;
    DWORD field_30;
    DWORD field_34;
    struct SDisplayMode* field_38_pnext;
    DWORD field_3C;
};
static_assert(sizeof(void*) != 4 || sizeof(SDisplayMode) == 0x40, "Wrong sized SDisplayMode");

struct SVidVersion
{
    DWORD mVersion;
    char mVersionString[255];
};

#define CC __stdcall

SVideo* CC Vid_Init_SYS(HINSTANCE param1, u16 param2_flags);
s32 CC Vid_CheckMode(SVideo* pVideoDriver, s32 width, s32 height, s32 rgbBitCount);
SDevice* CC Vid_FindDevice(SVideo* pVideoDriver, s32 deviceId);
SDisplayMode* CC Vid_FindMode(SVideo* pVideoDriver, s32 modeId);
s32 CC Vid_FindFirstMode(SVideo* pVideoDriver, s32 rgbBitCountFilter);
s32 CC Vid_FindNextMode(SVideo* pVideoDriver);
void CC Vid_CloseScreen(SVideo* pVideo);
s32 CC Vid_SetDevice(SVideo* pVideoDriver, s32 deviceId);
s32 CC Vid_SetMode(SVideo* pVideoDriver, HWND hWnd, s32 modeId);
void CC Vid_GrabSurface(SVideo* pVideoDriver);
void CC Vid_ReleaseSurface(SVideo* pVideoDriver);
void CC Vid_FlipBuffers(SVideo* pVideo);
void CC Vid_ShutDown_SYS(SVideo* pVideoDriver);
s32 CC Vid_EnableWrites(SVideo* pVideoDriver);
s32 CC Vid_DisableWrites(SVideo* pVideoDriver);
s32 CC Vid_GetSurface(SVideo* pVideoDriver);
s32 CC Vid_FreeSurface(SVideo* pVideoDriver);
s32 CC Vid_ClearScreen(SVideo* pVideoDriver, u8 aR, u8 aG, u8 aB, s32 aLeft, s32 aTop, s32 aRight, s32 aBottom);
s32 CC Vid_SetGamma(SVideo* pVideoDriver, f32 a2, f32 a3, f32 a4);
s32 CC Vid_WindowProc(SVideo* pVideoDriver, HWND hwnd, DWORD uMsg, WPARAM wParam, LPARAM lParam);
s32 CC Vid_InitDLL(HINSTANCE hInstance, SVideoFunctions* a2);
SVidVersion* CC Vid_GetVersion();

struct SVideoFunctions;

typedef SVidVersion*(__stdcall* T_Vid_GetVersion)();
typedef SVideo*(__stdcall* T_Vid_Init_SYS)(HINSTANCE param1, u16 param2_flags);
typedef s32(__stdcall* T_Vid_CheckMode)(SVideo* pVideoDriver, s32 width, s32 height, s32 rgbBitCount);
typedef SDisplayMode*(__stdcall* T_Vid_FindMode)(SVideo* pVideoDriver, s32 modeId);
typedef s32(__stdcall* T_Vid_FindFirstMode)(SVideo* pVideoDriver, s32 rgbBitCountFilter);
typedef s32(__stdcall* T_Vid_FindNextMode)(SVideo* pVideoDriver);
typedef SDevice*(__stdcall* T_Vid_FindDevice)(SVideo* pVideoDriver, s32 deviceId);
typedef s32(__stdcall* T_Vid_SetDevice)(SVideo* pVideoDriver, s32 deviceId);
typedef void(__stdcall* T_Vid_CloseScreen)(SVideo* pVideo);
typedef s32(__stdcall* T_Vid_SetMode)(SVideo* pVideoDriver, HWND hWnd, s32 modeId);
typedef void(__stdcall* T_Vid_FlipBuffers)(SVideo* pVideo);
typedef void(__stdcall* T_Vid_ReleaseSurface)(SVideo* pVideoDriver);
typedef void(__stdcall* T_Vid_GrabSurface)(SVideo* pVideoDriver);
typedef void(__stdcall* T_Vid_ShutDown_SYS)(SVideo* pVideoDriver);
typedef s32(__stdcall* T_Vid_EnableWrites)(SVideo* pVideoDriver);
typedef s32(__stdcall* T_Vid_DisableWrites)(SVideo* pVideoDriver);
typedef s32(__stdcall* T_Vid_GetSurface)(SVideo* pVideoDriver);
typedef s32(__stdcall* T_Vid_FreeSurface)(SVideo* pVideoDriver);
typedef s32(__stdcall* T_Vid_ClearScreen)(SVideo* pVideoDriver, u8 aR, u8 aG, u8 aB, s32 aLeft, s32 aTop, s32 aRight, s32 aBottom);
typedef s32(__stdcall* T_Vid_WindowProc)(SVideo* pVideoDriver, HWND hwnd, u32 uMsg, WPARAM wParam, LPARAM lParam);
typedef s32(__stdcall* T_Vid_InitDLL)(HINSTANCE hInstance, SVideoFunctions* a2);
typedef s32(__stdcall* T_Vid_SetGamma)(SVideo* pVideoDriver, f32 a2, f32 a3, f32 a4);


struct SVideoFunctions
{
    T_Vid_GetVersion* pVid_GetVersion;
    T_Vid_Init_SYS* pVid_Init_SYS;
    T_Vid_CheckMode* pVid_CheckMode;
    T_Vid_FindMode* pVid_FindMode;
    T_Vid_FindFirstMode* pVid_FindFirstMode;
    T_Vid_FindNextMode* pVid_FindNextMode;
    T_Vid_FindDevice* pVid_FindDevice;
    T_Vid_SetDevice* pVid_SetDevice;
    T_Vid_CloseScreen* pVid_CloseScreen;
    T_Vid_SetMode* pVid_SetMode;
    T_Vid_FlipBuffers* pVid_FlipBuffers;
    T_Vid_ReleaseSurface* pVid_ReleaseSurface;
    T_Vid_GrabSurface* pVid_GrabSurface;
    T_Vid_ShutDown_SYS* pVid_ShutDown_SYS;
    T_Vid_EnableWrites* pVid_EnableWrites;
    T_Vid_DisableWrites* pVid_DisableWrites;
    T_Vid_GetSurface* pVid_GetSurface;
    T_Vid_FreeSurface* pVid_FreeSurface;
    T_Vid_ClearScreen* pVid_ClearScreen;
    T_Vid_WindowProc* pVid_WindowProc;
    T_Vid_InitDLL* pVid_InitDLL;
    T_Vid_SetGamma* pVid_SetGamma;
};
template<class T>
inline void GetFunc(HINSTANCE hInstance, T& result, const char* name)
{
    result = reinterpret_cast<T>(GetProcAddress(hInstance, name));
}

template<class T>
inline void PopulateSVideoFunctions(HINSTANCE hDmaVideoDll, T& funcs)
{
    GetFunc(hDmaVideoDll, funcs.pVid_GetVersion, "Vid_GetVersion");
    GetFunc(hDmaVideoDll, funcs.pVid_Init_SYS, "Vid_Init_SYS");
    GetFunc(hDmaVideoDll, funcs.pVid_CheckMode, "Vid_CheckMode");
    GetFunc(hDmaVideoDll, funcs.pVid_FindMode, "Vid_FindMode");
    GetFunc(hDmaVideoDll, funcs.pVid_FindFirstMode, "Vid_FindFirstMode");
    GetFunc(hDmaVideoDll, funcs.pVid_FindNextMode, "Vid_FindNextMode");
    GetFunc(hDmaVideoDll, funcs.pVid_FindDevice, "Vid_FindDevice");
    GetFunc(hDmaVideoDll, funcs.pVid_SetDevice, "Vid_SetDevice");
    GetFunc(hDmaVideoDll, funcs.pVid_CloseScreen, "Vid_CloseScreen");
    GetFunc(hDmaVideoDll, funcs.pVid_SetMode, "Vid_SetMode");
    GetFunc(hDmaVideoDll, funcs.pVid_FlipBuffers, "Vid_FlipBuffers");
    GetFunc(hDmaVideoDll, funcs.pVid_ReleaseSurface, "Vid_ReleaseSurface");
    GetFunc(hDmaVideoDll, funcs.pVid_GrabSurface, "Vid_GrabSurface");
    GetFunc(hDmaVideoDll, funcs.pVid_ShutDown_SYS, "Vid_ShutDown_SYS");
    GetFunc(hDmaVideoDll, funcs.pVid_EnableWrites, "Vid_EnableWrites");
    GetFunc(hDmaVideoDll, funcs.pVid_DisableWrites, "Vid_DisableWrites");
    GetFunc(hDmaVideoDll, funcs.pVid_GetSurface, "Vid_GetSurface");
    GetFunc(hDmaVideoDll, funcs.pVid_FreeSurface, "Vid_FreeSurface");
    GetFunc(hDmaVideoDll, funcs.pVid_ClearScreen, "Vid_ClearScreen");
    GetFunc(hDmaVideoDll, funcs.pVid_WindowProc, "Vid_WindowProc");
    GetFunc(hDmaVideoDll, funcs.pVid_InitDLL, "Vid_InitDLL");
    GetFunc(hDmaVideoDll, funcs.pVid_SetGamma, "Vid_SetGamma");
}

#define RENDERER_API __declspec(dllexport)

struct _D3DPRESENT_PARAMETERS_;
typedef struct _D3DPRESENT_PARAMETERS_ D3DPRESENT_PARAMETERS;

struct STexture;

class Renderer
{
  public:
    struct CUSTOMVERTEX
    {
        FLOAT x, y, z, rhw; // D3DFVF_XYZW (pre-transformed vertex)
        DWORD diff; // D3DFVF_DIFFUSE
        DWORD spec; // D3DFVF_SPECULAR
        float u; // D3DFVF_TEX1
        float v; // D3DFVF_TEX1
    };

#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1)

    Renderer();
    ~Renderer();
    void InitD3D(HWND hWnd, s32 ogW, s32 ogH);
    RENDERER_API void BeginScene();
    RENDERER_API void EndScene();
    void Clear(u8 r, u8 g, u8 b, s32 left, s32 top, s32 right, s32 bottom);
    RENDERER_API void Draw(const CUSTOMVERTEX* pVerts, u32 vertsCount, struct IDirect3DTexture9* pTexture);
    RENDERER_API struct IDirect3DTexture9* SetTexture(IDirect3DTexture9* pExisting,
                                                u32 w,
                                                u32 h,
                                                const u8* pPixelData,
                                                const u16* pPalData,
                                                const s32* pitch);
    IDirect3DTexture9* SetFmvTexture(u32 w,
                                     u32 h,
                                     u32 wOffset,
                                     u32 hOffset,
                                     const u32* pPixelData);

    RENDERER_API void FreeTextureDelayed(IDirect3DTexture9* pTexture);
    void Present();
    RENDERER_API void SetRenderStates(u32 flags);
    RENDERER_API void TrackTexture(STexture* pTexture);
    RENDERER_API void UnTrackTexture(STexture* pTexture);
    void Resize(s32 w, s32 h);
    HWND GetHwnd() const
    {
        return mHwnd;
    }

  private:
    void DrawFmvTexture();
    void VerifyHR(HRESULT hr, const char* msg);
    void FreeTextures();
    void CreateVBO();
    void FreeVBO();
    void FixWindow();
    void GetCurWH();

    HWND mHwnd;
    struct IDirect3D9* g_pD3D;
    struct IDirect3DDevice9* g_pD3DDevice;
    struct IDirect3DVertexBuffer9* mVbo;
    std::vector<IDirect3DTexture9*> mFreeLaterTextures;
    D3DPRESENT_PARAMETERS* mPresentParams;
    bool mDeviceReady;
    std::set<STexture*> mTrackedTextures;
    float mOGW;
    float mOGH;
    float mCurW;
    float mCurH;
    IDirect3DTexture9* mFmvTexture;
};

class DmaVideoDataWrapper
{
  public:
    DmaVideoDataWrapper();
    ~DmaVideoDataWrapper();
    
    SVideo mVideo;
    Renderer mRenderer;
};
