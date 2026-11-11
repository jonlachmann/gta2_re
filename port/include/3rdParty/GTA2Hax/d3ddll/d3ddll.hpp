#pragma once

#include "../DmaVideo/DmaVideo.hpp"

#define CC __stdcall

typedef void (CC *TBeginSceneCB)();

struct STexture
{
    u16 field_0_id;
    u16 field_2;
    u16 field_4_pal_is_trans;
    u16 field_6_pal_size;
    void* field_8_locked_pixels_ptr;
    u8 field_C;
    u8 field_D;
    u16 field_E_width;
    u16 field_10_height;
    u8 field_12_bPalIsValid;
    u8 field_13_flags;
    BYTE* field_14_original_pixel_data_ptr;
    WORD* field_18_pPaltData;
    IDirect3DTexture9* field_1C_ptr; // +32 == IUnknown, DDSurface ptr ?
};
static_assert(sizeof(void*) != 4 || sizeof(STexture) == 0x20, "Wrong sized STexture");

struct Vert
{
    float x, y, z, w;
    DWORD diff;
    DWORD spec;
    float u, v;
};
static_assert(sizeof(Vert) == 0x20, "wrong size");

struct Verts
{
    Vert mVerts[4];
};

struct SLight
{
    DWORD field_0;  // ?? looking at map format maybe on/off time and strength/radius?
    float field_4_x;  // x ?
    float field_8_y;  // y ?
    float field_C_z;  // z ?
    DWORD field_10_colour; // ARGB?
};
static_assert(sizeof(SLight) == 0x14, "Wrong size SLight");

#pragma pack(push)
#pragma pack(1)
struct SImage
{
    BYTE field_0;
    BYTE field_1;
    BYTE field_2;
    BYTE field_3;
    DWORD field_4;
    DWORD field_8;
    WORD field_C_width;
    WORD field_E_height;
    BYTE field_10;
    BYTE field_11;
    DWORD field_12;
};
#pragma pack(pop)
static_assert(sizeof(SImage) == 0x16, "Wrong sized SImage");

void CC ConvertColourBank(s32 unknown);
int CC DrawLine(int a1, int a2, int a3, int a4, int a5);
void CC SetShadeTableA(int a1, int a2, int a3, int a4, int a5);
int* CC MakeScreenTable(int result, int a2, unsigned int a3);
int CC gbh_AddLight(SLight* pLight);
void CC gbh_AssignPalette(STexture* pTexture, int palId);
void CC gbh_BeginLevel();
// Note: Not part of OG API, added for ImGui call back for debug rendering
void CC gbh_SetBeginSceneCB(TBeginSceneCB pBeginSceneCB);
void CC gbh_BeginScene();
int CC gbh_BlitBuffer(int a1, int a2, int a3, int a4, int a5, int a6);
int CC gbh_BlitImage(int a1, int a2, int a3, int a4, int a5, int a6, int a7);
void CC gbh_CloseDLL();
void CC gbh_CloseScreen(SVideo* pVideo);
unsigned int CC gbh_Convert16BitGraphic(int a1, unsigned int a2, WORD *a3, signed int a4);
unsigned int CC gbh_ConvertColour(unsigned __int8 a1, unsigned __int8 a2, unsigned __int8 a3);
int CC gbh_DrawFlatRect(int a1, int a2);
void CC gbh_DrawQuad(int flags, STexture* pTexture, Vert* pVerts, int baseColour);
void CC gbh_DrawQuadClipped(int a1, int a2, int a3, int a4, int a5);
s32 CC gbh_DrawTilePart(unsigned int flags, STexture* pTexture, Vert* pData, int diffuseColour);
void CC gbh_DrawTriangle(int triFlags, STexture* pTexture, Vert* pVerts, int diffuseColour);
void CC gbh_EndLevel();
void CC gbh_EndScene();
int CC gbh_FreeImageTable();
void CC gbh_FreePalette(int a1);
void CC gbh_FreeTexture(STexture* pTexture);
u32* CC gbh_GetGlobals();
int CC gbh_GetUsedCache(int a1);
s32 CC gbh_Init(int a1);
u32 CC gbh_InitDLL(SVideo* pVideoDriver);
signed int CC gbh_InitImageTable(int tableSize);
signed int CC gbh_LoadImage(SImage* pImage);
STexture* CC gbh_LockTexture(STexture* pTexture);
void CC gbh_Plot(int a1, int a2, int a3, int a4);
int CC gbh_PrintBitmap(int a1, int a2);
unsigned int CC gbh_RegisterPalette(int a1, DWORD *a2);
STexture* CC gbh_RegisterTexture(s32 width, s32 height, BYTE* pData, s32 pal_idx, char_type flag);
void CC gbh_ResetLights();
void CC gbh_SetAmbient(float a1);
int CC gbh_SetCamera(float a1, float a2, float a3, float a4);
int CC gbh_SetColourDepth();
void CC gbh_SetWindow(float left, float top, float right, float bottom);
STexture* CC gbh_UnlockTexture(STexture* pTexture);

