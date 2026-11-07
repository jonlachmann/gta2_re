// gbh_* render API on top of IRenderer. Logic ported from GTA2Hax d3ddll.cpp; portable C++. The scene, drawing
// and the exports; the textures and lights are in gbh_textures.cpp and gbh_lighting.cpp.
#include "../dev/gbh_log.hpp"
#include "gbh_internal.hpp"
#include <stdlib.h>
#include <string.h>

namespace gbh
{
IRenderer* gRenderer;
} // namespace gbh

using namespace gbh;

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

SGlobals gGlobals;
f32 gWindow[4];
s32 gScreenTable[1700];
u32 gOptionsGeneration = ~0u;

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

// Enhancement: follows option changes (live toggle, ini edits); palettes and textures are converted again.
void SyncOptions()
{
    if (!gRenderer || gRenderer->Options().mGeneration == gOptionsGeneration)
    {
        return;
    }
    gOptionsGeneration = gRenderer->Options().mGeneration;
    SetTrueColour(gRenderer->Options().mTrueColour);
    MarkLightsDirty();
}

// D3D ARGB to the renderer's RGBA byte order.
u32 ArgbToRgba(u32 c)
{
    return ((c >> 16) & 0xFF) | (c & 0xFF00) | ((c & 0xFF) << 16) | (c & 0xFF000000u);
}

const u32 FLAG_INTERNAL_BLIT = 0x80000000u;

void RenderVerts(u32 flags, STexture* pTexture, Vert* pVerts, s32 count, u32 baseColour)
{
    if (count < 3 || count > 4 || pVerts[0].z <= 0.0f || !gRenderer)
    {
        return;
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
    const bool lit = (flags & FLAG_LIGHTING) && LightingOn();
    const bool perPixel = lit && gRenderer->Options().mPerPixelLighting; // enhancement
    u32 unlit[4];
    if (lit)
    {
        for (s32 i = 0; i < count; i++)
        {
            unlit[i] = pVerts[i].diff;
            pVerts[i].diff = LightVertex(pVerts[i + 4], shade, pVerts[i].diff);
        }
        if (perPixel)
        {
            UploadLightsIfDirty();
        }
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

    GbhLog_Prim(pVerts, count, state, pTexture, (flags & FLAG_INTERNAL_BLIT) != 0);

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
    InitLighting();
#if defined(__i386__) || defined(_M_IX86)
    // As DMA's d3ddll: x87 single precision, rounding down, for the whole process (game code included).
    unsigned short cw;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    cw = (unsigned short)((cw & ~0x0F00) | 0x0400);
    __asm__ __volatile__("fldcw %0" : : "m"(cw));
#endif
    GLVideoHeader* pHeader = (GLVideoHeader*)pVideo;
    if (!pHeader || pHeader->mMagic != kGLVideoMagic)
    {
        gRenderer = 0;
        return 0;
    }
    gRenderer = pHeader->mRenderer;
    GbhLog_Init(pHeader->mFlipCounter);
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
    SyncOptions();
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

GBH_EXPORT s32 GTA2_CC gbh_SetCamera(f32, f32, f32, f32)
{
    return 0;
}

GBH_EXPORT void GTA2_CC gbh_SetWindow(f32 left, f32 top, f32 right, f32 bottom)
{
    gWindow[0] = left;
    gWindow[1] = top;
    gWindow[2] = right;
    gWindow[3] = bottom;
}

GBH_EXPORT u32 GTA2_CC gbh_ConvertColour(u8 r, u8 g, u8 b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

GBH_EXPORT u32 GTA2_CC gbh_Convert16BitGraphic(s32, u32, u16*, s32)
{
    return 0;
}

GBH_EXPORT void GTA2_CC gbh_DrawQuad(s32 flags, STexture* pTexture, Vert* pVerts, s32 baseColour)
{
    GbhLog_Call("DrawQuad", flags, pTexture, baseColour);
    RenderVerts((u32)flags, pTexture, pVerts, 4, (u32)baseColour & 0xFF);
}

GBH_EXPORT void GTA2_CC gbh_DrawTriangle(s32 flags, STexture* pTexture, Vert* pVerts, s32 diffuseColour)
{
    GbhLog_Call("DrawTriangle", flags, pTexture, diffuseColour);
    RenderVerts((u32)flags, pTexture, pVerts, 3, (u32)diffuseColour & 0xFF);
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

GBH_EXPORT s32 GTA2_CC gbh_DrawTilePart(u32 flags, STexture* pTexture, Vert* pData, s32 diffuseColour)
{
    GbhLog_Call("DrawTilePart", flags, pTexture, diffuseColour);
    return DrawTileImpl(flags, pTexture, pData, diffuseColour);
}

GBH_EXPORT void GTA2_CC gbh_DrawQuadClipped(s32, s32, s32, s32, s32)
{
}

GBH_EXPORT s32 GTA2_CC gbh_DrawFlatRect(Vert* pVerts, s32 colour)
{
    GbhLog_Call("DrawFlatRect", 0, 0, colour);
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

GBH_EXPORT s32 GTA2_CC gbh_BlitImage(s32 imageIndex, s32 srcLeft, s32 srcTop, s32 srcRight, s32 srcBottom, s32 dstX, s32 dstY)
{
    GbhLog_Call("BlitImage", imageIndex, 0, 0);
    const ImageEntry* pImg = Image(imageIndex);
    if (!pImg)
    {
        return -1;
    }
    const ImageEntry& img = *pImg;
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
    GbhLog_Call("DrawTile", flags, pTexture, diffuseColour);
    return DrawTileImpl(flags, pTexture, pData, diffuseColour);
}
