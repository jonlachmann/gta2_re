// The gbh_* layer's parts (gbh_api.cpp: the scene, drawing and the exports; gbh_textures.cpp: palettes, textures and
// images; gbh_lighting.cpp: the level's lights), as the original's d3ddll.dll keeps them.
#pragma once

#include "gbh_api.hpp"
#include "renderer.hpp"
#include <vector>

namespace gbh
{
extern IRenderer* gRenderer; // the video side's renderer (gbh_InitDLL)

// ---- gbh_textures.cpp

// Texture variants, as the original keeps two palettes per palette id: opaque draws use the
// opaque texture format (RGB565 in the reference), draws with alpha flags the alpha format (ARGB4444).
enum
{
    kOpaque = 0,
    kAlpha = 1,
};

// field_18_pPaltData of a texture points at m565, the start of this struct.
struct PalEntry
{
    u16 m565[512];
    u32 mRgba[2][256];
    u32 mRaw[256]; // game palette colours, 0x00RRGGBB
};

// field_1C_ptr of a texture.
struct TexData
{
    RTexture* mTex[2];
    bool mDirty[2];
    bool mImage; // 16-bit TGA image, already converted to mPixels
    u32 mEpoch; // gTextureEpoch of the last conversion
    std::vector<u32> mPixels;
};

struct ImageEntry
{
    s32 mLoaded;
    u32 mWidth;
    u32 mHeight;
    STexture* mTexture;
};

// The original keeps textures in square cache slots of 8..256 texels (0xe107e0), with the last column
// and row repeated once; UVs are normalised by the slot size. Same here, so UVs match bit for bit.
s32 SlotSize(s32 w, s32 h);
TexData* Data(STexture* pTexture);
// Converts on first use: the game registers textures whose pixels are only valid later (or never).
RTexture* Resolve(STexture* pTexture, int variant);
// Enhancement: the palettes and textures converted again for true colour on or off
void SetTrueColour(bool trueColour);
const ImageEntry* Image(s32 index); // a loaded image (gbh_LoadImage), else NULL

// ---- gbh_lighting.cpp

void InitLighting();
bool LightingOn(); // the ambient light is below full, so lit draws are lit
// A vertex's colour lit by the level's lights, as the original's LightVertex
u32 LightVertex(const Vert& vert, u8 shade, u32 colour);
void MarkLightsDirty(); // enhancement: the per pixel lights uploaded again before the next lit draw
void UploadLightsIfDirty();
} // namespace gbh
