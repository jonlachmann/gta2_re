// The DirectDraw declarations the game still compiles against: the DirectX version probe (never run
// by the port) and the video structure of DMA's video DLL.
#pragma once

#include "windows.h"

DEFINE_GUID(IID_IDirectDraw2, 0xB3A6F3E0, 0x2B43, 0x11CF, 0xA2, 0xDE, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56);
DEFINE_GUID(IID_IDirectDrawSurface3, 0xDA044E00, 0x69B2, 0x11D0, 0xA1, 0xD5, 0x00, 0xAA, 0x00, 0xB8, 0xDF, 0xBB);

#define DDSCL_NORMAL 0x00000008
#define DDSD_CAPS 0x00000001
#define DDSCAPS_PRIMARYSURFACE 0x00000200

typedef struct _DDSCAPS
{
    DWORD dwCaps;
} DDSCAPS;

typedef struct _DDSCAPS2
{
    DWORD dwCaps;
    DWORD dwCaps2;
    DWORD dwCaps3;
    DWORD dwCaps4;
} DDSCAPS2;

typedef struct _DDCOLORKEY
{
    DWORD dwColorSpaceLowValue;
    DWORD dwColorSpaceHighValue;
} DDCOLORKEY;

typedef struct _DDPIXELFORMAT
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwRGBAlphaBitMask;
} DDPIXELFORMAT;

typedef struct _DDSURFACEDESC
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG lPitch;
    DWORD dwBackBufferCount;
    DWORD dwRefreshRate;
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay;
    DDCOLORKEY ddckCKDestBlt;
    DDCOLORKEY ddckCKSrcOverlay;
    DDCOLORKEY ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS ddsCaps;
} DDSURFACEDESC;

typedef struct _DDSURFACEDESC2
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG lPitch;
    DWORD dwBackBufferCount;
    DWORD dwRefreshRate;
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay;
    DDCOLORKEY ddckCKDestBlt;
    DDCOLORKEY ddckCKSrcOverlay;
    DDCOLORKEY ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS2 ddsCaps;
    DWORD dwTextureStage;
} DDSURFACEDESC2;

// DDCAPS of DirectX 7: only held by value in DMA's video structure.
typedef struct _DDCAPS
{
    DWORD dwSize;
    BYTE mFields[0x17C - 4];
} DDCAPS;

#if defined(__i386__) || defined(_M_IX86)
static_assert(sizeof(DDSURFACEDESC) == 0x6C && sizeof(DDSURFACEDESC2) == 0x7C && sizeof(DDCAPS) == 0x17C, "DirectDraw layout");
#endif

DECLARE_INTERFACE_(IDirectDrawSurface, IUnknown){};
typedef IDirectDrawSurface* LPDIRECTDRAWSURFACE;

DECLARE_INTERFACE_(IDirectDraw, IUnknown)
{
    STDMETHOD(Compact)() PURE;
    STDMETHOD(CreateClipper)(DWORD flags, LPVOID* ppClipper, LPUNKNOWN outer) PURE;
    STDMETHOD(CreatePalette)(DWORD flags, LPVOID pEntries, LPVOID* ppPalette, LPUNKNOWN outer) PURE;
    STDMETHOD(CreateSurface)(DDSURFACEDESC* pDesc, LPDIRECTDRAWSURFACE* ppSurface, LPUNKNOWN outer) PURE;
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE* ppDst) PURE;
    STDMETHOD(EnumDisplayModes)(DWORD flags, DDSURFACEDESC* pDesc, LPVOID pContext, LPVOID pCallback) PURE;
    STDMETHOD(EnumSurfaces)(DWORD flags, DDSURFACEDESC* pDesc, LPVOID pContext, LPVOID pCallback) PURE;
    STDMETHOD(FlipToGDISurface)() PURE;
    STDMETHOD(GetCaps)(DDCAPS* pDriverCaps, DDCAPS* pHelCaps) PURE;
    STDMETHOD(GetDisplayMode)(DDSURFACEDESC* pDesc) PURE;
    STDMETHOD(GetFourCCCodes)(LPDWORD pCount, LPDWORD pCodes) PURE;
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE* ppSurface) PURE;
    STDMETHOD(GetMonitorFrequency)(LPDWORD pFrequency) PURE;
    STDMETHOD(GetScanLine)(LPDWORD pLine) PURE;
    STDMETHOD(GetVerticalBlankStatus)(BOOL* pInBlank) PURE;
    STDMETHOD(Initialize)(GUID* pGuid) PURE;
    STDMETHOD(RestoreDisplayMode)() PURE;
    STDMETHOD(SetCooperativeLevel)(HWND h, DWORD flags) PURE;
    STDMETHOD(SetDisplayMode)(DWORD width, DWORD height, DWORD bpp) PURE;
    STDMETHOD(WaitForVerticalBlank)(DWORD flags, HANDLE event) PURE;
};
typedef IDirectDraw* LPDIRECTDRAW;

struct IDirectDraw4;
struct IDirectDraw7;
struct IDirectDrawClipper;
typedef IDirectDrawClipper* LPDIRECTDRAWCLIPPER;
