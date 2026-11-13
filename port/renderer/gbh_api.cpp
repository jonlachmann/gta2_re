// gbh_* render API on top of IRenderer. Logic ported from GTA2Hax d3ddll.cpp; portable C++.
#include "gbh_api.hpp"
#include "port_exports.hpp"
#include "cmdlog.hpp"
#include "renderer.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace
{
struct SGlobals
{
    u32 mNumPolysDrawn;
    u32 mNumTextureSwaps;
    u32 mNumBatchFlushes;
    u32 mSceneTime;
    u32 mCacheSizes[12];
    u32 mCacheSizes2[12];
    u32 mCacheHitRates[12];
    u32 mCacheUnknown[12];
    void* mCaches[12];
};

const u32 kSGlobalsValues[] = {10, 62, 62, 88, 1, 0, 110, 126, 126, 40, 10, 0};

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
    u32 mEpoch;  // gTextureEpoch of the last conversion
    std::vector<u32> mPixels;
};

struct ImageEntry
{
    s32 mLoaded;
    u32 mWidth;
    u32 mHeight;
    STexture* mTexture;
};

struct LightInternal
{
    u32 mFlags;
    f32 mBrightness;
    f32 mRadius;
    f32 mRadiusSquared;
    f32 mInvRadius;
    f32 mX, mY, mZ;
    f32 mR, mG, mB;
    f32 mDx, mDy, mWidth, mBeam;
    f32 mCeiling; // the height its light fades out above (port_LampLight)
};

IRenderer* gRenderer;
SGlobals gGlobals;
PalEntry* gPals[16384];
std::vector<ImageEntry> gImages;
LightInternal gLights[256];
u32 gNumLights;
// Whether the last gbh_AddLight found room and kept the light, for port_LampLight.
bool gLightAdded;
// The blocks MapRenderer walks at layer 0, its widest (gbh_SetCamera): lights that reach none are left out.
f32 gLightArea[4];
bool gLightAreaSet;
// Red, green and blue; tinted only by port_SetAmbient.
f32 gAmbient[3] = {255.0f, 255.0f, 255.0f};
// The game's and the port's lights scaled by it (the DAY-NIGHT CYCLE's fade).
f32 gLightScale = 1.0f;
f32 gWindow[4];
s32 gScreenTable[1700];
u32 gTextureId;
u32 gSceneCount;
CmdLog gCmd;
const u32* gFlipCounter;

void LogCall(const char* func, u32 flags, STexture* t, u32 colour)
{
    if (gFlipCounter)
    {
        gCmd.Update(*gFlipCounter);
    }
    gCmd.Call(func, flags, t ? t->field_0_id : 0, t ? t->field_E_width : 0, t ? t->field_10_height : 0, colour & 0xFF);
}

// Same fields as the reference log of the D3D calls: D3D render state values, texel units.
void LogPrim(const Vert* pVerts, s32 count, u32 state, STexture* t, bool blit)
{
    if (!gCmd.Active())
    {
        return;
    }
    u32 st[5] = {0, 0, 0, 2, 2};
    if (state & RS_BLEND_ADD)
    {
        st[0] = 1, st[1] = 2, st[2] = 2;
    }
    else if (state & RS_BLEND_ALPHA)
    {
        st[0] = 1, st[1] = 5, st[2] = 6;
    }
    if (!(state & RS_FILTER_LINEAR))
    {
        st[3] = st[4] = 1;
    }
    gCmd.Prim(blit ? "blit" : (count == 4 ? "fan" : "list"), count, st, t ? t->field_0_id : 0, t ? t->field_E_width : 0,
              t ? t->field_10_height : 0);
    for (s32 i = 0; i < count; i++)
    {
        gCmd.Vertex(pVerts[i].x, pVerts[i].y, pVerts[i].z, pVerts[i].w, pVerts[i].diff, pVerts[i].spec, pVerts[i].u, pVerts[i].v);
    }
}
u32 gTraceScene; // GTA2GL_TRACE=<scene>: logs the draw calls of that scene to stderr
bool gTrueColour;   // option: 8-bit palette channels instead of the original's 16-bit textures
bool gMipmaps;      // option: textures uploaded with mip levels
u32 gOptionsGeneration = ~0u;
u32 gTextureEpoch;  // bumped when every texture has to be converted again
bool gLightsDirty = true;
std::vector<u32> gConvertBuffer;

// The original keeps textures in square cache slots of 8..256 texels (0xe107e0), with the last column
// and row repeated once; UVs are normalised by the slot size. Same here, so UVs match bit for bit.
s32 SlotSize(s32 w, s32 h)
{
    const s32 m = w > h ? w : h;
    s32 slot = 8;
    while (slot < m && slot < 256)
    {
        slot <<= 1;
    }
    return slot < m ? m : slot;
}

const u32 FLAG_FIT_TO_TEXTURE = 0x10000;
const u32 FLAG_NO_FILTER = 0x20000;
const u32 FLAG_LIGHTING = 0x8000;
const u32 FLAG_TILE_UV = 0x4000;
const u32 FLAG_VERTEX_DIFFUSE = 0x2000;
const u32 FLAG_BLEND_ADD = 0x200;
const u32 FLAG_BLEND_ALPHA = 0x100;
const u32 FLAG_COLOUR_KEY = 0x80;

void InitGlobals()
{
    memset(&gGlobals, 0, sizeof(gGlobals));
    for (int i = 0; i < 12; i++)
    {
        gGlobals.mCacheUnknown[i] = kSGlobalsValues[i];
    }
}

u16 To565(u32 rgb)
{
    const u32 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (u16)(((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255));
}

TexData* Data(STexture* pTexture)
{
    return pTexture ? (TexData*)pTexture->field_1C_ptr : 0;
}

STexture* NewTexture(s32 width, s32 height, u8* pData)
{
    STexture* pTexture = (STexture*)malloc(sizeof(STexture));
    if (!pTexture)
    {
        return 0;
    }
    memset(pTexture, 0, sizeof(STexture));
    pTexture->field_0_id = (u16)gTextureId++;
    pTexture->field_E_width = (u16)width;
    pTexture->field_10_height = (u16)height;
    pTexture->field_14_original_pixel_data_ptr = pData;
    TexData* pData2 = new TexData();
    pData2->mTex[kOpaque] = 0;
    pData2->mTex[kAlpha] = 0;
    pData2->mDirty[kOpaque] = pData2->mDirty[kAlpha] = true;
    pData2->mImage = false;
    pData2->mEpoch = gTextureEpoch;
    pTexture->field_1C_ptr = pData2;
    return pTexture;
}

// Converts on first use: the game registers textures whose pixels are only valid later (or never).
RTexture* Resolve(STexture* pTexture, int variant)
{
    TexData* pData = Data(pTexture);
    if (!pData || !gRenderer)
    {
        return 0;
    }
    if (pData->mImage)
    {
        variant = kOpaque;
    }
    if (!pData->mTex[variant])
    {
        pData->mTex[variant] = gRenderer->CreateTexture();
    }
    if (pData->mEpoch != gTextureEpoch)
    {
        pData->mEpoch = gTextureEpoch;
        pData->mDirty[kOpaque] = pData->mDirty[kAlpha] = true;
    }
    if (pData->mDirty[variant])
    {
        const s32 w = pTexture->field_E_width;
        const s32 h = pTexture->field_10_height;
        if (pData->mImage)
        {
            gRenderer->UploadTexture(pData->mTex[variant], w, h, &pData->mPixels[0], RMIP_NONE);
        }
        else if (pTexture->field_14_original_pixel_data_ptr && pTexture->field_18_pPaltData && w > 0 && h > 0)
        {
            const u32* pal = ((const PalEntry*)pTexture->field_18_pPaltData)->mRgba[variant];
            const s32 slot = SlotSize(w, h);
            gConvertBuffer.assign((size_t)slot * slot, 0);
            for (s32 y = 0; y < h; y++)
            {
                // Sprite and tile pages are 256 pixels wide.
                const u8* src = pTexture->field_14_original_pixel_data_ptr + (size_t)y * 256;
                u32* dst = &gConvertBuffer[(size_t)y * slot];
                for (s32 x = 0; x < w; x++)
                {
                    dst[x] = pal[src[x]];
                }
                if (w < slot)
                {
                    dst[w] = dst[w - 1];
                }
            }
            if (h < slot)
            {
                memcpy(&gConvertBuffer[(size_t)h * slot], &gConvertBuffer[(size_t)(h - 1) * slot], (size_t)slot * 4);
            }
            gRenderer->UploadTexture(pData->mTex[variant], slot, slot, &gConvertBuffer[0], variant == kAlpha ? RMIP_ALPHA : RMIP_OPAQUE);
        }
        else
        {
            return 0;
        }
        pData->mDirty[variant] = false;
    }
    return pData->mTex[variant];
}

void ConvertPalette(PalEntry* pPal)
{
    for (s32 i = 0; i < 256; i++)
    {
        const u32 c = pPal->mRaw[i];
        const u32 r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        const u32 a = i == 0 ? 0 : 0xFF000000u;
        if (gTrueColour)
        {
            pPal->mRgba[kOpaque][i] = pPal->mRgba[kAlpha][i] = r | (g << 8) | (b << 16) | a;
            continue;
        }
        // 16-bit textures: bits truncated, expanded by replication when sampled.
        pPal->mRgba[kOpaque][i] = ((r & 0xF8) | (r >> 5)) | (((g & 0xFC) | (g >> 6)) << 8) | (((b & 0xF8) | (b >> 5)) << 16) | a;
        pPal->mRgba[kAlpha][i] = ((r >> 4) * 17) | (((g >> 4) * 17) << 8) | (((b >> 4) * 17) << 16) | a;
    }
}

// Follows option changes (live toggle, ini edits): palettes and textures are converted again.
void SyncOptions()
{
    if (!gRenderer || gRenderer->Options().mGeneration == gOptionsGeneration)
    {
        return;
    }
    gOptionsGeneration = gRenderer->Options().mGeneration;
    const bool trueColour = gRenderer->Options().mTrueColour;
    if (trueColour != gTrueColour)
    {
        gTrueColour = trueColour;
        for (s32 i = 0; i < 16384; i++)
        {
            if (gPals[i])
            {
                ConvertPalette(gPals[i]);
            }
        }
        gTextureEpoch++;
    }
    const bool mipmaps = gRenderer->Options().mTextureFilter > 0;
    if (mipmaps != gMipmaps)
    {
        gMipmaps = mipmaps;
        gTextureEpoch++;
    }
    gLightsDirty = true;
}

void UploadLights()
{
    static RLight sLights[256];
    s32 n = 0;
    for (u32 j = 0; j < gNumLights; j++)
    {
        const LightInternal& l = gLights[j];
        if ((l.mFlags & 0x30000) != 0x10000)
        {
            continue;
        }
        RLight& o = sLights[n++];
        o.x = l.mX, o.y = l.mY, o.z = l.mZ;
        o.radius = l.mRadius;
        o.ceiling = l.mCeiling;
        o.r = l.mR * l.mBrightness, o.g = l.mG * l.mBrightness, o.b = l.mB * l.mBrightness;
        o.dx = l.mDx, o.dy = l.mDy, o.width = l.mWidth, o.beam = l.mBeam;
    }
    gRenderer->SetLights(sLights, n, gAmbient, 1.0f - gLightScale);
    gLightsDirty = false;
}

// The original's table driven square root (12 bit mantissa index, 4096 entries).
u32 gSqrtTable[4096];

void InitSqrtTable()
{
    for (u32 i = 0; i < 2048; i++)
    {
        union { u32 u; f32 f; } a, b;
        a.u = (i << 12) | 0x3F800000u; // [1, 2)
        b.u = (i << 12) | 0x40000000u; // [2, 4)
        a.f = (f32)sqrt((double)a.f);
        b.f = (f32)sqrt((double)b.f);
        gSqrtTable[i] = a.u & 0x7FFFFF;
        gSqrtTable[2048 + i] = b.u & 0x7FFFFF;
    }
}

f32 TableSqrt(f32 x)
{
    union { u32 u; f32 f; } v;
    v.f = x;
    u32 mant = v.u & 0x7FFFFF;
    u32 exp = (v.u & 0x7F800000u) >> 1;
    if (exp & 0x400000)
    {
        exp += 0x20000000u;
    }
    else
    {
        exp += 0x1F800000u;
        mant |= 0x800000;
    }
    v.u = gSqrtTable[mant >> 12] | (exp & 0x7F800000u);
    return v.f;
}

f32 SmoothStep(f32 t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// A beam's light at (dx, dy) from its lamp, 0 to 2: a glow on the lamp and a wide soft cone, gaussian
// across (sigma the light's width, opening about 60 degrees), fading fast along. Nothing behind the lamp.
f32 Beam(const LightInternal& light, f32 dx, f32 dy)
{
    const f32 r = light.mRadius;
    const f32 along = dx * light.mDx + dy * light.mDy;
    const f32 ahead = along > 0.0f ? along : 0.0f;
    const f32 across2 = dx * dx + dy * dy - along * along;
    const f32 sigma = light.mWidth + 0.58f * ahead;
    const f32 cone = expf(-ahead / (0.33f * r)) * expf(-across2 / (2.0f * sigma * sigma));
    const f32 glow = expf(-(dx * dx + dy * dy) / (2.0f * 0.12f * 0.12f));
    return SmoothStep((along + 0.02f) / 0.1f) * (1.0f - SmoothStep((along - 0.7f * r) / (0.3f * r))) * (cone + glow);
}

// The most the beams together add to a point (the port's headlights), so that a group of cars
// doesn't light a whole junction; the game's own lights add up freely.
const f32 kSpotCap = 0.6f;

f32 Cap(f32 v)
{
    return v > kSpotCap ? kSpotCap : v;
}

// A light is gone this far above its ceiling (also in the shader's LightSum).
const f32 kCeilingFade = 0.4f;

f32 BelowCeiling(const LightInternal& light, f32 z)
{
    return 1.0f - SmoothStep((z - light.mCeiling) / kCeilingFade);
}

// The ambient light on a colour channel. As the lights fade out (the DAY-NIGHT CYCLE's day) it takes on the
// unlit colour, so that at full ambient it draws as unlit.
f32 Ambient(s32 i, u32 channel)
{
    return gAmbient[i] - (1.0f - gLightScale) * (255 - channel) * (gAmbient[i] / 255.0f);
}

// The lights that can reach a primitive, in their order: those LightVertex skips at every corner are left out.
u16 gNearLights[256];

u32 NearLights(const Vert* pWorld, s32 count)
{
    f32 x0 = pWorld[0].x, x1 = x0, y0 = pWorld[0].y, y1 = y0, z0 = pWorld[0].z, z1 = z0;
    for (s32 i = 1; i < count; i++)
    {
        x0 = pWorld[i].x < x0 ? pWorld[i].x : x0;
        x1 = pWorld[i].x > x1 ? pWorld[i].x : x1;
        y0 = pWorld[i].y < y0 ? pWorld[i].y : y0;
        y1 = pWorld[i].y > y1 ? pWorld[i].y : y1;
        z0 = pWorld[i].z < z0 ? pWorld[i].z : z0;
        z1 = pWorld[i].z > z1 ? pWorld[i].z : z1;
    }
    u32 n = 0;
    for (u32 j = 0; j < gNumLights; j++)
    {
        const LightInternal& light = gLights[j];
        if ((light.mFlags & 0x30000) != 0x10000)
        {
            continue;
        }
        // No corner is nearer on any axis; the margin covers rounding against LightVertex's own distance.
        const f32 dx = light.mX < x0 ? x0 - light.mX : (light.mX > x1 ? light.mX - x1 : 0.0f);
        const f32 dy = light.mY < y0 ? y0 - light.mY : (light.mY > y1 ? light.mY - y1 : 0.0f);
        const f32 dz = light.mZ < z0 ? z0 - light.mZ : (light.mZ > z1 ? light.mZ - z1 : 0.0f);
        if (dx * dx + dy * dy + dz * dz > light.mRadiusSquared * 1.001f)
        {
            continue;
        }
        gNearLights[n++] = (u16)j;
    }
    return n;
}

// numNear: the primitive's NearLights.
u32 LightVertex(const Vert& vert, u8 shade, u32 colour, u32 numNear)
{
    f32 lr = 0.0f, lg = 0.0f, lb = 0.0f;
    f32 sr = 0.0f, sg = 0.0f, sb = 0.0f;
    for (u32 k = 0; k < numNear; k++)
    {
        const LightInternal& light = gLights[gNearLights[k]];
        if ((light.mFlags & 0x30000) != 0x10000)
        {
            continue;
        }
        const f32 dx = vert.x - light.mX;
        const f32 dy = vert.y - light.mY;
        const f32 dz = vert.z - light.mZ;
        const f32 d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > light.mRadiusSquared)
        {
            continue;
        }
        const f32 dist = TableSqrt(d2);
        const f32 att = (light.mRadius - dist) * light.mInvRadius;
        if (att > 0.0f)
        {
            const bool spot = light.mBeam > 0.0f;
            const f32 k = light.mBrightness * (spot ? Beam(light, dx, dy) : att) * BelowCeiling(light, vert.z);
            (spot ? sr : lr) += light.mR * k;
            (spot ? sg : lg) += light.mG * k;
            (spot ? sb : lb) += light.mB * k;
        }
    }
    lr += Cap(sr);
    lg += Cap(sg);
    lb += Cap(sb);
    const f32 n = shade / 255.0f;
    // Rounded while the lights fade, so that it meets the unlit colour.
    const f32 round = gLightScale < 1.0f ? 0.5f : 0.0f;
    f32 r = n * ((colour >> 16) & 0xFF) * lr + Ambient(0, (colour >> 16) & 0xFF) + round;
    f32 g = n * ((colour >> 8) & 0xFF) * lg + Ambient(1, (colour >> 8) & 0xFF) + round;
    f32 b = n * (colour & 0xFF) * lb + Ambient(2, colour & 0xFF) + round;
    const u32 ri = r > 255.0f ? 255 : (u32)r;
    const u32 gi = g > 255.0f ? 255 : (u32)g;
    const u32 bi = b > 255.0f ? 255 : (u32)b;
    return (colour & 0xFF000000u) | (ri << 16) | (gi << 8) | bi;
}

// D3D ARGB to the renderer's RGBA byte order.
u32 ArgbToRgba(u32 c)
{
    return ((c >> 16) & 0xFF) | (c & 0xFF00) | ((c & 0xFF) << 16) | (c & 0xFF000000u);
}

void Trace(const char* what, u32 flags, STexture* pTexture, const Vert* pVerts, s32 count, u32 colour)
{
    fprintf(stderr, "%s f=%05x tex=%u(%ux%u) c=%u", what, flags, pTexture ? pTexture->field_0_id : 0,
            pTexture ? pTexture->field_E_width : 0, pTexture ? pTexture->field_10_height : 0, colour);
    for (s32 i = 0; i < count; i++)
    {
        fprintf(stderr, " [%.3f %.3f %.6f d=%08x uv=%.3f %.3f]", pVerts[i].x, pVerts[i].y, pVerts[i].z, pVerts[i].diff,
                pVerts[i].u, pVerts[i].v);
    }
    fprintf(stderr, "\n");
}

const u32 FLAG_INTERNAL_BLIT = 0x80000000u;
// The port's sprite shadow (port/src/shadows.cpp): a quad with world positions in pVerts[4..7] and its blur
// radius in the red channel (texels * 32), drawn soft when the option is on.
const u32 FLAG_SOFT_SHADOW = 0x40000000u;

// Grows a sprite quad (corners in the order of increasing u, then v) by its blur radius plus a texel.
void ExpandShadow(Vert* pVerts)
{
    const f32 du = pVerts[1].u - pVerts[0].u;
    const f32 dv = pVerts[3].v - pVerts[0].v;
    if (du <= 0.0f || dv <= 0.0f)
    {
        return;
    }
    const f32 e = ((pVerts[0].diff >> 16) & 0xFF) / 32.0f + 1.0f;
    for (s32 k = 0; k < 8; k += 4)
    {
        const f32 ux = (pVerts[k + 1].x - pVerts[k].x) / du, uy = (pVerts[k + 1].y - pVerts[k].y) / du;
        const f32 vx = (pVerts[k + 3].x - pVerts[k].x) / dv, vy = (pVerts[k + 3].y - pVerts[k].y) / dv;
        for (s32 i = 0; i < 4; i++)
        {
            const f32 su = i == 1 || i == 2 ? e : -e;
            const f32 sv = i >= 2 ? e : -e;
            pVerts[k + i].x += su * ux + sv * vx;
            pVerts[k + i].y += su * uy + sv * vy;
        }
    }
    for (s32 i = 0; i < 4; i++)
    {
        pVerts[i].u += i == 1 || i == 2 ? e : -e;
        pVerts[i].v += i >= 2 ? e : -e;
    }
}

void RenderVerts(u32 flags, STexture* pTexture, Vert* pVerts, s32 count, u32 baseColour)
{
    if (gTraceScene && gSceneCount == gTraceScene)
    {
        Trace("draw", flags, pTexture, pVerts, count, baseColour);
    }
    if (count < 3 || count > 4 || pVerts[0].z <= 0.0f || !gRenderer)
    {
        return;
    }
    const bool softShadow = (flags & FLAG_SOFT_SHADOW) && count == 4 && gRenderer->Options().mSoftShadows;
    if (softShadow)
    {
        ExpandShadow(pVerts);
    }
    // As the original: skip primitives entirely outside the gbh_SetWindow rectangle.
    f32 minX = pVerts[0].x, maxX = minX, minY = pVerts[0].y, maxY = minY;
    for (s32 i = 1; i < count; i++)
    {
        minX = pVerts[i].x < minX ? pVerts[i].x : minX;
        maxX = pVerts[i].x > maxX ? pVerts[i].x : maxX;
        minY = pVerts[i].y < minY ? pVerts[i].y : minY;
        maxY = pVerts[i].y > maxY ? pVerts[i].y : maxY;
    }
    if (!(flags & FLAG_INTERNAL_BLIT) && (minX > gWindow[2] || maxX < gWindow[0] || minY > gWindow[3] || maxY < gWindow[1]))
    {
        return;
    }
    if (flags & FLAG_FIT_TO_TEXTURE)
    {
        flags |= FLAG_NO_FILTER;
    }

    RTexture* pTex = pTexture ? Resolve(pTexture, (flags & (FLAG_BLEND_ADD | FLAG_BLEND_ALPHA | FLAG_COLOUR_KEY)) ? kAlpha : kOpaque) : 0;
    const f32 texW = pTexture && pTexture->field_E_width ? (f32)pTexture->field_E_width : 1.0f;
    const f32 texH = pTexture && pTexture->field_10_height ? (f32)pTexture->field_10_height : 1.0f;
    const bool image = pTexture && Data(pTexture) && Data(pTexture)->mImage;
    const f32 slotScale = image ? 1.0f : 1.0f / (f32)SlotSize((s32)texW, (s32)texH);
    const f32 scaleU = image ? 1.0f / texW : slotScale;
    const f32 scaleV = image ? 1.0f / texH : slotScale;

    if (flags & FLAG_FIT_TO_TEXTURE)
    {
        // The original's quad: (x0, y0) .. (x0 + w - 0.001, y0 + h - 0.001), UVs 0 .. w - 0.001.
        const f32 x1 = pVerts[0].x + texW - 0.001f;
        const f32 y1 = pVerts[0].y + texH - 0.001f;
        const f32 u1 = texW - 0.001f;
        const f32 v1 = texH - 0.001f;
        pVerts[1].x = x1;
        pVerts[1].y = pVerts[0].y;
        pVerts[2].x = x1;
        pVerts[2].y = y1;
        pVerts[3].x = pVerts[0].x;
        pVerts[3].y = y1;
        pVerts[1].z = pVerts[2].z = pVerts[3].z = pVerts[0].z;
        pVerts[0].u = 0.0f;
        pVerts[0].v = 0.0f;
        pVerts[1].u = u1;
        pVerts[1].v = 0.0f;
        pVerts[2].u = u1;
        pVerts[2].v = v1;
        pVerts[3].u = 0.0f;
        pVerts[3].v = v1;
    }
    for (s32 i = 0; i < count; i++)
    {
        pVerts[i].w = pVerts[i].z;
        pVerts[i].spec = 0;
    }

    const u8 shade = (u8)baseColour;
    if (!(flags & FLAG_VERTEX_DIFFUSE))
    {
        const u32 diffuse = shade | (shade << 8) | (shade << 16) | 0xFF000000u;
        for (s32 i = 0; i < count; i++)
        {
            pVerts[i].diff = diffuse;
        }
    }
    const bool fullAmbient = gAmbient[0] == 255.0f && gAmbient[1] == 255.0f && gAmbient[2] == 255.0f;
    const bool lit = (flags & FLAG_LIGHTING) && !fullAmbient;
    const bool shadowLit = softShadow && !fullAmbient;
    const bool perPixel = (lit || shadowLit) && gRenderer->Options().mPerPixelLighting;
    u32 unlit[4];
    const u32 numNear = lit || (shadowLit && !perPixel) ? NearLights(pVerts + 4, count) : 0;
    if (lit)
    {
        for (s32 i = 0; i < count; i++)
        {
            unlit[i] = pVerts[i].diff;
            pVerts[i].diff = LightVertex(pVerts[i + 4], shade, pVerts[i].diff, numNear);
        }
    }
    else if (shadowLit)
    {
        // A shadow takes away the ambient light and half of the lights', so it fades in a lit pool but not in
        // the dark (per pixel in the shader).
        for (s32 i = 0; i < count; i++)
        {
            unlit[i] = 0xFFFFFFFFu;
            if (!perPixel)
            {
                const u32 l = LightVertex(pVerts[i + 4], 255, 0xFFFFFFu, numNear);
                const f32 sum = (f32)(((l >> 16) & 0xFF) + ((l >> 8) & 0xFF) + (l & 0xFF));
                const f32 ratio = (gAmbient[0] + gAmbient[1] + gAmbient[2]) / (sum > 1.0f ? sum : 1.0f);
                const u32 a = (u32)((pVerts[i].diff >> 24) * (0.5f + 0.5f * (ratio < 1.0f ? ratio : 1.0f)));
                pVerts[i].diff = (pVerts[i].diff & 0xFFFFFFu) | (a << 24);
            }
        }
    }
    if (perPixel && gLightsDirty)
    {
        UploadLights();
    }

    // As the original: additive is ONE/ONE, the colour key and alpha flags both use alpha blending
    // with the (filtered) texture alpha times the vertex alpha. No alpha test.
    u32 state = 0;
    if (flags & FLAG_BLEND_ADD)
    {
        state |= RS_BLEND_ADD;
    }
    else if (flags & (FLAG_BLEND_ALPHA | FLAG_COLOUR_KEY))
    {
        state |= RS_BLEND_ALPHA;
    }
    if (!(flags & FLAG_NO_FILTER))
    {
        state |= RS_FILTER_LINEAR;
    }
    if (flags & FLAG_INTERNAL_BLIT)
    {
        state |= RS_PIXEL_EXACT;
    }
    if (softShadow)
    {
        state |= RS_SHADOW;
    }

    LogPrim(pVerts, count, state, pTexture, (flags & FLAG_INTERNAL_BLIT) != 0);

    RVertex rv[4];
    for (s32 i = 0; i < count; i++)
    {
        rv[i].x = pVerts[i].x;
        rv[i].y = pVerts[i].y;
        rv[i].rhw = pVerts[i].z;
        rv[i].rgba = ArgbToRgba(pVerts[i].diff);
        rv[i].u = pVerts[i].u * scaleU;
        rv[i].v = pVerts[i].v * scaleV;
        if (perPixel)
        {
            rv[i].wx = pVerts[i + 4].x;
            rv[i].wy = pVerts[i + 4].y;
            rv[i].wz = pVerts[i + 4].z;
            rv[i].unlit = ArgbToRgba(unlit[i]);
            rv[i].shade = shade / 255.0f;
            rv[i].lit = 1.0f;
        }
        else
        {
            rv[i].wx = rv[i].wy = rv[i].wz = 0.0f;
            rv[i].unlit = 0;
            rv[i].shade = 0.0f;
            rv[i].lit = 0.0f;
        }
    }
    RVertex tris[6];
    s32 n = 3;
    tris[0] = rv[0];
    tris[1] = rv[1];
    tris[2] = rv[2];
    if (count == 4)
    {
        tris[3] = rv[0];
        tris[4] = rv[2];
        tris[5] = rv[3];
        n = 6;
    }
    if (gTraceScene && gSceneCount == gTraceScene)
    {
        fprintf(stderr, "  -> amb=%.1f lights=%u state=%x diff=%08x %08x %08x %08x\n", gAmbient[0], gNumLights, state, pVerts[0].diff,
                pVerts[1].diff, pVerts[2].diff, count > 3 ? pVerts[3].diff : 0);
    }
    gRenderer->DrawTriangles(tris, n, pTex, state);
    gGlobals.mNumPolysDrawn += count - 2;
}

void DrawColouredQuad(f32 l, f32 t, f32 r, f32 b, u32 argb)
{
    if (!gRenderer)
    {
        return;
    }
    RVertex v[6];
    const f32 xs[6] = {l, r, r, l, r, l};
    const f32 ys[6] = {t, t, b, t, b, b};
    for (int i = 0; i < 6; i++)
    {
        v[i].x = xs[i];
        v[i].y = ys[i];
        v[i].rhw = 1.0f;
        v[i].rgba = ArgbToRgba(argb);
        v[i].u = v[i].v = 0.0f;
        v[i].wx = v[i].wy = v[i].wz = 0.0f;
        v[i].unlit = 0;
        v[i].shade = 0.0f;
        v[i].lit = 0.0f;
    }
    gRenderer->DrawTriangles(v, 6, 0, 0);
}
} // namespace

GBH_EXPORT void GTA2_CC ConvertColourBank(void*)
{
}

GBH_EXPORT s32 GTA2_CC DrawLine(s32, s32 a2, s32, s32 a4, s32)
{
    return abs(a4 - a2);
}

GBH_EXPORT void GTA2_CC SetShadeTableA(s32, s32, s32, s32, s32)
{
}

GBH_EXPORT s32* GTA2_CC MakeScreenTable(s32 value, s32 elementSize, u32 size)
{
    if (size > sizeof(gScreenTable) / sizeof(gScreenTable[0]))
    {
        size = sizeof(gScreenTable) / sizeof(gScreenTable[0]);
    }
    s32* p = gScreenTable;
    for (u32 i = 0; i < size; i++)
    {
        *p++ = value;
        value += elementSize;
    }
    return p;
}

GBH_EXPORT u32 GTA2_CC gbh_InitDLL(SVideo* pVideo)
{
    InitGlobals();
    InitSqrtTable();
#if defined(__i386__) || defined(_M_IX86)
    // As DMA's d3ddll: x87 single precision, rounding down, for the whole process (game code included).
    const char* pFpu = getenv("GTA2GL_KEEP_FPU");
    if (!pFpu || !atoi(pFpu))
    {
        unsigned short cw;
        __asm__ __volatile__("fnstcw %0" : "=m"(cw));
        cw = (unsigned short)((cw & ~0x0F00) | 0x0400);
        __asm__ __volatile__("fldcw %0" : : "m"(cw));
    }
#endif
    const char* pTrace = getenv("GTA2GL_TRACE");
    gTraceScene = pTrace ? (u32)atoi(pTrace) : 0;
    GLVideoHeader* pHeader = (GLVideoHeader*)pVideo;
    if (!pHeader || pHeader->mMagic != kGLVideoMagic)
    {
        gRenderer = 0;
        return 0;
    }
    gRenderer = pHeader->mRenderer;
    gFlipCounter = pHeader->mFlipCounter;
    gCmd.Init("gl");
    return 1;
}

GBH_EXPORT void GTA2_CC gbh_CloseDLL()
{
}

GBH_EXPORT s32 GTA2_CC gbh_Init(s32)
{
    return 0;
}

GBH_EXPORT void GTA2_CC gbh_CloseScreen(SVideo*)
{
}

GBH_EXPORT void GTA2_CC gbh_BeginLevel()
{
}

GBH_EXPORT void GTA2_CC gbh_EndLevel()
{
}

GBH_EXPORT void GTA2_CC gbh_SetBeginSceneCB(void*)
{
}

GBH_EXPORT void GTA2_CC gbh_BeginScene()
{
    gGlobals.mNumPolysDrawn = 0;
    gSceneCount++;
    SyncOptions();
#if defined(__i386__)
    if (gTraceScene && gSceneCount == gTraceScene)
    {
        unsigned short cw;
        __asm__ __volatile__("fnstcw %0" : "=m"(cw));
        fprintf(stderr, "scene %u fpu cw %04x\n", gSceneCount, cw);
    }
#endif
    if (gRenderer)
    {
        gRenderer->BeginScene();
    }
}

GBH_EXPORT void GTA2_CC gbh_EndScene()
{
    if (gRenderer)
    {
        gRenderer->EndScene();
        gGlobals.mNumBatchFlushes = gRenderer->GetDrawCallCount();
    }
}

GBH_EXPORT u32* GTA2_CC gbh_GetGlobals()
{
    return (u32*)&gGlobals;
}

GBH_EXPORT s32 GTA2_CC gbh_GetUsedCache(s32)
{
    return 0;
}

GBH_EXPORT s32 GTA2_CC gbh_SetColourDepth()
{
    return 1;
}

GBH_EXPORT s32 GTA2_CC gbh_SetCamera(f32 minX, f32 minY, f32 maxX, f32 maxY)
{
    gLightArea[0] = minX, gLightArea[1] = minY, gLightArea[2] = maxX + 1.0f, gLightArea[3] = maxY + 1.0f;
    gLightAreaSet = true;
    return 0;
}

GBH_EXPORT void GTA2_CC gbh_SetWindow(f32 left, f32 top, f32 right, f32 bottom)
{
    gWindow[0] = left;
    gWindow[1] = top;
    gWindow[2] = right;
    gWindow[3] = bottom;
    if (gRenderer)
    {
        gRenderer->SetClipRect(left, top, right, bottom);
    }
}

GBH_EXPORT u32 GTA2_CC gbh_ConvertColour(u8 r, u8 g, u8 b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

GBH_EXPORT u32 GTA2_CC gbh_Convert16BitGraphic(s32, u32, u16*, s32)
{
    return 0;
}

GBH_EXPORT u32 GTA2_CC gbh_RegisterPalette(s32 palId, u32* pData)
{
    if (palId < 0 || palId >= 16384 || !pData)
    {
        return 0;
    }
    // Entries are stored in columns: entry i of a palette is 64 dwords after entry i - 1.
    u32* p = pData;
    for (s32 i = 0; i < 256; i++, p += 64)
    {
        if (i == 0)
        {
            *p = 0;
        }
        else if (*p == 0)
        {
            *p = 0x10000;
        }
    }
    PalEntry* pPal = gPals[palId];
    if (!pPal)
    {
        pPal = new PalEntry();
        gPals[palId] = pPal;
    }
    p = pData;
    for (s32 i = 0; i < 256; i++, p += 64)
    {
        pPal->mRaw[i] = *p & 0xFFFFFF;
        pPal->m565[i] = To565(*p);
        pPal->m565[i + 256] = pPal->m565[i];
    }
    ConvertPalette(pPal);
    return palId;
}

GBH_EXPORT void GTA2_CC gbh_FreePalette(s32 palId)
{
    if (palId >= 0 && palId < 16384)
    {
        delete gPals[palId];
        gPals[palId] = 0;
    }
}

GBH_EXPORT STexture* GTA2_CC gbh_LockTexture(STexture* pTexture)
{
    pTexture->field_8_locked_pixels_ptr = pTexture->field_14_original_pixel_data_ptr;
    pTexture->field_13_flags |= 1;
    pTexture->field_6_pal_size = 256;
    return pTexture;
}

GBH_EXPORT STexture* GTA2_CC gbh_UnlockTexture(STexture* pTexture)
{
    pTexture->field_6_pal_size = 0;
    pTexture->field_8_locked_pixels_ptr = 0;
    pTexture->field_13_flags &= 0xFE;
    TexData* pData = Data(pTexture);
    if (pData)
    {
        pData->mDirty[kOpaque] = pData->mDirty[kAlpha] = true;
    }
    return pTexture;
}

GBH_EXPORT void GTA2_CC gbh_AssignPalette(STexture* pTexture, s32 palId)
{
    const bool needUnlock = !(pTexture->field_13_flags & 1);
    if (needUnlock)
    {
        gbh_LockTexture(pTexture);
    }
    PalEntry* pPal = (palId >= 0 && palId < 16384) ? gPals[palId] : 0;
    pTexture->field_18_pPaltData = pPal ? pPal->m565 : 0;
    pTexture->field_12_bPalIsValid = pPal ? 1 : 0;
    if (needUnlock)
    {
        gbh_UnlockTexture(pTexture);
    }
}

GBH_EXPORT STexture* GTA2_CC gbh_RegisterTexture(s32 width, s32 height, u8* pData, s32 palId, char)
{
    STexture* pTexture = NewTexture(width, height, pData);
    if (pTexture)
    {
        PalEntry* pPal = (palId >= 0 && palId < 16384) ? gPals[palId] : 0;
        pTexture->field_18_pPaltData = pPal ? pPal->m565 : 0;
        pTexture->field_12_bPalIsValid = pPal ? 1 : 0;
    }
    return pTexture;
}

GBH_EXPORT void GTA2_CC gbh_FreeTexture(STexture* pTexture)
{
    if (!pTexture)
    {
        return;
    }
    TexData* pData = Data(pTexture);
    if (pData)
    {
        for (int v = 0; v < 2; v++)
        {
            if (gRenderer && pData->mTex[v])
            {
                gRenderer->FreeTexture(pData->mTex[v]);
            }
        }
        delete pData;
    }
    free(pTexture);
}

GBH_EXPORT void GTA2_CC gbh_ResetLights()
{
    gNumLights = 0;
    gLightsDirty = true;
}

GBH_EXPORT s32 GTA2_CC gbh_AddLight(SLight* pLight)
{
    gLightAdded = gNumLights < 256;
    if (!gLightAdded)
    {
        return 0;
    }
    const u32 idx = gNumLights++;
    gLightsDirty = true;
    LightInternal& l = gLights[idx];
    const f32 k = 1.0f / 255.0f;
    l.mFlags = pLight->field_0;
    l.mBrightness = (pLight->field_0 & 0xFF) * k * gLightScale;
    l.mX = pLight->field_4_x;
    l.mY = pLight->field_8_y;
    l.mZ = pLight->field_C_z;
    l.mR = ((pLight->field_10_colour >> 16) & 0xFF) * k;
    l.mG = ((pLight->field_10_colour >> 8) & 0xFF) * k;
    l.mB = (pLight->field_10_colour & 0xFF) * k;
    l.mRadius = ((pLight->field_0 >> 8) & 0xFF) * k * 8.0f;
    l.mRadiusSquared = l.mRadius * l.mRadius;
    l.mInvRadius = l.mRadius > 0.0f ? 1.0f / l.mRadius : 0.0f;
    l.mDx = l.mDy = l.mWidth = l.mBeam = 0.0f;
    l.mCeiling = 1e6f;
    // The game submits the lights of 8 to 12 blocks around the walk; a sprite may stick out of it by a block or two.
    const f32 kMargin = 2.0f;
    const f32 dx = l.mX < gLightArea[0] ? gLightArea[0] - l.mX : (l.mX > gLightArea[2] ? l.mX - gLightArea[2] : 0.0f);
    const f32 dy = l.mY < gLightArea[1] ? gLightArea[1] - l.mY : (l.mY > gLightArea[3] ? l.mY - gLightArea[3] : 0.0f);
    const f32 reach = l.mRadius + kMargin;
    if (gLightAreaSet && dx * dx + dy * dy > reach * reach)
    {
        gNumLights--;
        gLightAdded = false;
        return 0;
    }
    return idx * 0x2C;
}

// The port's lamps, near the ground: the last light added lights little more than half a block above it (not
// a bridge over a car), and becomes a beam along (dx, dy) when width > 0, its range the light's radius.
void port_LampLight(f32 dx, f32 dy, f32 width)
{
    if (gLightAdded)
    {
        LightInternal& l = gLights[gNumLights - 1];
        l.mCeiling = l.mZ + 0.5f;
        if (width > 0.0f)
        {
            l.mDx = dx, l.mDy = dy, l.mWidth = width, l.mBeam = 1.0f;
        }
    }
}

GBH_EXPORT void GTA2_CC gbh_SetAmbient(f32 ambient)
{
    gAmbient[0] = gAmbient[1] = gAmbient[2] = ambient * 255.0f;
    gLightsDirty = true;
}

// The level's ambient light in colour, and the scale of the lights added after it.
void port_SetAmbient(f32 r, f32 g, f32 b, f32 lights)
{
    gAmbient[0] = r * 255.0f;
    gAmbient[1] = g * 255.0f;
    gAmbient[2] = b * 255.0f;
    gLightScale = lights;
    gLightsDirty = true;
}

// TWIN STICK's reticle (port/src/twin_stick.cpp): an aged bronze ring round a red lamp, shaded as the HUD's sprites
// (lit from the top left), size pixels wide at (x, y).
void port_DrawReticle(f32 x, f32 y, f32 size, f32 alpha)
{
    static RTexture* spTex;
    static IRenderer* spOwner;
    if (!gRenderer)
    {
        return;
    }
    const s32 n = 32;
    if (spOwner != gRenderer)
    {
        u32 pixels[n * n];
        const f32 lx = -0.55f, ly = -0.55f, lz = 0.63f; // the light
        for (s32 i = 0; i < n * n; i++)
        {
            const f32 dx = i % n + 0.5f - n / 2, dy = i / n + 0.5f - n / 2;
            const f32 r = sqrtf(dx * dx + dy * dy) + 1e-4f;
            const bool lamp = r < 6.0f;
            // Half round profiles: the lamp's radius 2.2, the ring's 1.4 about radius 11; t the normal's tilt.
            const f32 t = lamp ? r / 2.2f : (r - 11.0f) / 1.4f;
            const f32 a = 0.85f * fminf(fmaxf((lamp ? 2.2f - r : 1.4f - fabsf(r - 11.0f)) + 0.5f, 0.0f), 1.0f);
            const f32 tc = fminf(fabsf(t), 1.0f) * (t < 0 ? -1.0f : 1.0f);
            const f32 nx = dx / r * tc, ny = dy / r * tc, nz = sqrtf(1.0f - tc * tc);
            const f32 diffuse = fmaxf(nx * lx + ny * ly + nz * lz, 0.0f);
            const f32 spec = powf(fmaxf((nx * lx + ny * ly + nz * (lz + 1.0f)) / sqrtf(lx * lx + ly * ly + (lz + 1) * (lz + 1)), 0.0f), 24.0f);
            const f32 base[3] = {lamp ? 230.0f : 105.0f, lamp ? 40.0f : 78.0f, lamp ? 25.0f : 45.0f};
            u32 c = static_cast<u32>(a * 255.0f) << 24;
            for (s32 k = 0; k < 3; k++)
            {
                c |= static_cast<u32>(fminf(base[k] * (0.25f + 0.85f * diffuse) + 200.0f * spec, 255.0f)) << (8 * k);
            }
            pixels[i] = c;
        }
        spTex = gRenderer->CreateTexture();
        gRenderer->UploadTexture(spTex, n, n, pixels, RMIP_ALPHA);
        spOwner = gRenderer;
    }
    const u32 rgba = 0xFFFFFFu | (static_cast<u32>(fminf(fmaxf(alpha, 0.0f), 1.0f) * 255.0f) << 24);
    const f32 h = size / 2;
    const f32 corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    RVertex v[4] = {};
    for (s32 i = 0; i < 4; i++)
    {
        v[i].x = x + h * corners[i][0];
        v[i].y = y + h * corners[i][1];
        v[i].rhw = 1.0f;
        v[i].rgba = rgba;
        v[i].u = corners[i][0] > 0 ? 1.0f : 0.0f;
        v[i].v = corners[i][1] > 0 ? 1.0f : 0.0f;
    }
    const RVertex tris[6] = {v[0], v[1], v[2], v[0], v[2], v[3]};
    gRenderer->DrawTriangles(tris, 6, spTex, RS_BLEND_ALPHA | RS_FILTER_LINEAR);
}

GBH_EXPORT void GTA2_CC gbh_DrawQuad(s32 flags, STexture* pTexture, Vert* pVerts, s32 baseColour)
{
    LogCall("DrawQuad", flags, pTexture, baseColour);
    RenderVerts((u32)flags, pTexture, pVerts, 4, (u32)baseColour & 0xFF);
}

GBH_EXPORT void GTA2_CC gbh_DrawTriangle(s32 flags, STexture* pTexture, Vert* pVerts, s32 diffuseColour)
{
    LogCall("DrawTriangle", flags, pTexture, diffuseColour);
    RenderVerts((u32)flags, pTexture, pVerts, 3, (u32)diffuseColour & 0xFF);
}

static s32 DrawTileImpl(u32 flags, STexture* pTexture, Vert* pData, s32 diffuseColour);

GBH_EXPORT s32 GTA2_CC gbh_DrawTilePart(u32 flags, STexture* pTexture, Vert* pData, s32 diffuseColour)
{
    LogCall("DrawTilePart", flags, pTexture, diffuseColour);
    return DrawTileImpl(flags, pTexture, pData, diffuseColour);
}

static s32 DrawTileImpl(u32 flags, STexture* pTexture, Vert* pData, s32 diffuseColour)
{
    u32 drawFlags = flags;
    if (!(flags & FLAG_TILE_UV))
    {
        // Half texel inset against bleeding at the tile edges.
        pData[0].u = 0.5f;
        pData[0].v = 0.5f;
        pData[1].u = 63.499901f;
        pData[1].v = 0.5f;
        pData[2].u = 63.499901f;
        pData[2].v = 63.499901f;
        pData[3].u = 0.5f;
        pData[3].v = 63.499901f;
    }

    f32 uvs[4][2];
    for (int i = 0; i < 4; i++)
    {
        uvs[i][0] = pData[i].u;
        uvs[i][1] = pData[i].v;
    }

    // Rotation in 90 degree steps (0x40 is a 180 degree turn, done as two flips).
    static const int kRot90[4] = {3, 0, 1, 2};
    static const int kRot270[4] = {1, 2, 3, 0};
    const int* pRot = 0;
    switch (flags & 0x60)
    {
        case 0x20:
            pRot = kRot90;
            break;
        case 0x40:
            drawFlags = flags ^ 0x18;
            break;
        case 0x60:
            pRot = kRot270;
            break;
    }
    if (pRot)
    {
        for (int i = 0; i < 4; i++)
        {
            pData[i].u = uvs[pRot[i]][0];
            pData[i].v = uvs[pRot[i]][1];
        }
        for (int i = 0; i < 4; i++)
        {
            uvs[i][0] = pData[i].u;
            uvs[i][1] = pData[i].v;
        }
    }

    if (drawFlags & 8)
    {
        static const int kFlip[4] = {1, 0, 3, 2};
        for (int i = 0; i < 4; i++)
        {
            pData[i].u = uvs[kFlip[i]][0];
            pData[i].v = uvs[kFlip[i]][1];
        }
        if (drawFlags & 0x10)
        {
            for (int i = 0; i < 4; i++)
            {
                uvs[i][0] = pData[i].u;
                uvs[i][1] = pData[i].v;
            }
        }
    }
    if (drawFlags & 0x10)
    {
        static const int kFlip[4] = {3, 2, 1, 0};
        for (int i = 0; i < 4; i++)
        {
            pData[i].u = uvs[kFlip[i]][0];
            pData[i].v = uvs[kFlip[i]][1];
        }
    }

    RenderVerts(drawFlags, pTexture, pData, 4, (u32)diffuseColour & 0xFF);
    return 0;
}

GBH_EXPORT void GTA2_CC gbh_DrawQuadClipped(s32, s32, s32, s32, s32)
{
}

GBH_EXPORT s32 GTA2_CC gbh_DrawFlatRect(Vert* pVerts, s32 colour)
{
    LogCall("DrawFlatRect", 0, 0, colour);
    if (!pVerts)
    {
        return 0;
    }
    // As the original: untextured, no blending, colour as given.
    DrawColouredQuad(pVerts[0].x, pVerts[0].y, pVerts[2].x, pVerts[2].y, (u32)colour);
    return 0;
}

// A no-op in DMA's d3ddll.dll as well.
GBH_EXPORT void GTA2_CC gbh_Plot(f32, f32, s32, s32)
{
}

GBH_EXPORT s32 GTA2_CC gbh_PrintBitmap(s32, s32)
{
    return 0;
}

GBH_EXPORT s32 GTA2_CC gbh_BlitBuffer(s32, s32, s32, s32, s32, s32)
{
    return 0;
}

GBH_EXPORT s32 GTA2_CC gbh_InitImageTable(s32 tableSize)
{
    if (tableSize < 0)
    {
        return -1;
    }
    gImages.assign(tableSize, ImageEntry());
    for (size_t i = 0; i < gImages.size(); i++)
    {
        memset(&gImages[i], 0, sizeof(ImageEntry));
    }
    return 0;
}

GBH_EXPORT s32 GTA2_CC gbh_FreeImageTable()
{
    for (size_t i = 0; i < gImages.size(); i++)
    {
        if (gImages[i].mLoaded)
        {
            gbh_FreeTexture(gImages[i].mTexture);
        }
    }
    gImages.clear();
    return 0;
}

// The buffer is a whole TGA file (type 2, 16 bpp X1R5G5B5); the game frees it right after the call.
GBH_EXPORT s32 GTA2_CC gbh_LoadImage(SImage* pImage)
{
    s32 slot = -1;
    for (size_t i = 0; i < gImages.size(); i++)
    {
        if (!gImages[i].mLoaded)
        {
            slot = (s32)i;
            break;
        }
    }
    if (slot < 0)
    {
        return -1;
    }
    if (!pImage || pImage->field_1_cmap_type || pImage->field_2_image_type != 2 || pImage->field_10_bpp != 16)
    {
        return -2;
    }
    const s32 w = pImage->field_C_width;
    const s32 h = pImage->field_E_height;
    STexture* pTexture = NewTexture(w, h, 0);
    if (!pTexture)
    {
        return -3;
    }
    TexData* pData = Data(pTexture);
    pData->mImage = true;
    pData->mPixels.resize((size_t)w * h);
    const u16* src = (const u16*)((const u8*)pImage + offsetof(SImage, field_12) + pImage->field_0_id_len);
    const bool topDown = (pImage->field_11_descriptor & 0x20) != 0;
    for (s32 y = 0; y < h; y++)
    {
        const u16* row = src + (size_t)(topDown ? y : (h - 1 - y)) * w;
        u32* dst = &pData->mPixels[(size_t)y * w];
        for (s32 x = 0; x < w; x++)
        {
            const u32 p = row[x];
            // Green goes 5 -> 6 bits by a shift, as the original's 555 to 565 conversion.
            const u32 r = (p >> 10) & 31, g = ((p >> 5) & 31) << 1, b = p & 31;
            dst[x] = ((r << 3) | (r >> 2)) | (((g << 2) | (g >> 4)) << 8) | (((b << 3) | (b >> 2)) << 16) | 0xFF000000u;
        }
    }
    gImages[slot].mLoaded = 1;
    gImages[slot].mWidth = w;
    gImages[slot].mHeight = h;
    gImages[slot].mTexture = pTexture;
    return slot;
}

GBH_EXPORT s32 GTA2_CC gbh_BlitImage(s32 imageIndex, s32 srcLeft, s32 srcTop, s32 srcRight, s32 srcBottom, s32 dstX, s32 dstY)
{
    LogCall("BlitImage", imageIndex, 0, 0);
    if (imageIndex < 0 || imageIndex >= (s32)gImages.size() || !gImages[imageIndex].mLoaded)
    {
        return -1;
    }
    const ImageEntry& img = gImages[imageIndex];
    if (srcLeft < 0 || srcTop < 0 || srcRight <= srcLeft || srcBottom <= srcTop || srcRight > (s32)img.mWidth ||
        srcBottom > (s32)img.mHeight)
    {
        return -2;
    }
    Vert verts[4];
    memset(verts, 0, sizeof(verts));
    const f32 w = (f32)(srcRight - srcLeft);
    const f32 h = (f32)(srcBottom - srcTop);
    const f32 xs[4] = {0, w, w, 0};
    const f32 ys[4] = {0, 0, h, h};
    for (int i = 0; i < 4; i++)
    {
        verts[i].x = dstX + xs[i];
        verts[i].y = dstY + ys[i];
        verts[i].z = 1.0f;
        verts[i].w = 1.0f;
        verts[i].u = srcLeft + xs[i];
        verts[i].v = srcTop + ys[i];
    }
    // The original does a DirectDraw Blt here: an exact copy.
    RenderVerts(FLAG_NO_FILTER | FLAG_INTERNAL_BLIT, img.mTexture, verts, 4, 255);
    return 0;
}

GBH_EXPORT s32 GTA2_CC gbh_DrawTile(u32 flags, STexture* pTexture, Vert* pData, s32 diffuseColour)
{
    LogCall("DrawTile", flags, pTexture, diffuseColour);
    return DrawTileImpl(flags, pTexture, pData, diffuseColour);
}

// The render DLL's export table, which the game reaches through LoadLibrary/GetProcAddress.
const PortExport kGbhExports[] = {
    {"ConvertColourBank", reinterpret_cast<void*>(&ConvertColourBank)},
    {"DrawLine", reinterpret_cast<void*>(&DrawLine)},
    {"SetShadeTableA", reinterpret_cast<void*>(&SetShadeTableA)},
    {"MakeScreenTable", reinterpret_cast<void*>(&MakeScreenTable)},
    {"gbh_InitDLL", reinterpret_cast<void*>(&gbh_InitDLL)},
    {"gbh_CloseDLL", reinterpret_cast<void*>(&gbh_CloseDLL)},
    {"gbh_Init", reinterpret_cast<void*>(&gbh_Init)},
    {"gbh_CloseScreen", reinterpret_cast<void*>(&gbh_CloseScreen)},
    {"gbh_BeginLevel", reinterpret_cast<void*>(&gbh_BeginLevel)},
    {"gbh_EndLevel", reinterpret_cast<void*>(&gbh_EndLevel)},
    {"gbh_SetBeginSceneCB", reinterpret_cast<void*>(&gbh_SetBeginSceneCB)},
    {"gbh_BeginScene", reinterpret_cast<void*>(&gbh_BeginScene)},
    {"gbh_EndScene", reinterpret_cast<void*>(&gbh_EndScene)},
    {"gbh_GetGlobals", reinterpret_cast<void*>(&gbh_GetGlobals)},
    {"gbh_GetUsedCache", reinterpret_cast<void*>(&gbh_GetUsedCache)},
    {"gbh_SetColourDepth", reinterpret_cast<void*>(&gbh_SetColourDepth)},
    {"gbh_SetCamera", reinterpret_cast<void*>(&gbh_SetCamera)},
    {"gbh_SetWindow", reinterpret_cast<void*>(&gbh_SetWindow)},
    {"gbh_ConvertColour", reinterpret_cast<void*>(&gbh_ConvertColour)},
    {"gbh_Convert16BitGraphic", reinterpret_cast<void*>(&gbh_Convert16BitGraphic)},
    {"gbh_RegisterPalette", reinterpret_cast<void*>(&gbh_RegisterPalette)},
    {"gbh_FreePalette", reinterpret_cast<void*>(&gbh_FreePalette)},
    {"gbh_LockTexture", reinterpret_cast<void*>(&gbh_LockTexture)},
    {"gbh_UnlockTexture", reinterpret_cast<void*>(&gbh_UnlockTexture)},
    {"gbh_AssignPalette", reinterpret_cast<void*>(&gbh_AssignPalette)},
    {"gbh_RegisterTexture", reinterpret_cast<void*>(&gbh_RegisterTexture)},
    {"gbh_FreeTexture", reinterpret_cast<void*>(&gbh_FreeTexture)},
    {"gbh_ResetLights", reinterpret_cast<void*>(&gbh_ResetLights)},
    {"gbh_AddLight", reinterpret_cast<void*>(&gbh_AddLight)},
    {"gbh_SetAmbient", reinterpret_cast<void*>(&gbh_SetAmbient)},
    {"gbh_DrawQuad", reinterpret_cast<void*>(&gbh_DrawQuad)},
    {"gbh_DrawTriangle", reinterpret_cast<void*>(&gbh_DrawTriangle)},
    {"gbh_DrawTilePart", reinterpret_cast<void*>(&gbh_DrawTilePart)},
    {"gbh_DrawQuadClipped", reinterpret_cast<void*>(&gbh_DrawQuadClipped)},
    {"gbh_DrawFlatRect", reinterpret_cast<void*>(&gbh_DrawFlatRect)},
    {"gbh_Plot", reinterpret_cast<void*>(&gbh_Plot)},
    {"gbh_PrintBitmap", reinterpret_cast<void*>(&gbh_PrintBitmap)},
    {"gbh_BlitBuffer", reinterpret_cast<void*>(&gbh_BlitBuffer)},
    {"gbh_InitImageTable", reinterpret_cast<void*>(&gbh_InitImageTable)},
    {"gbh_FreeImageTable", reinterpret_cast<void*>(&gbh_FreeImageTable)},
    {"gbh_LoadImage", reinterpret_cast<void*>(&gbh_LoadImage)},
    {"gbh_BlitImage", reinterpret_cast<void*>(&gbh_BlitImage)},
    {"gbh_DrawTile", reinterpret_cast<void*>(&gbh_DrawTile)},
};
const int kGbhExportCount = sizeof(kGbhExports) / sizeof(kGbhExports[0]);
