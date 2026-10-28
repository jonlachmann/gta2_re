// The gbh_* layer's palettes, textures and images (gbh_internal.hpp).
#include "gbh_internal.hpp"
#include <stdlib.h>
#include <string.h>

namespace gbh
{
namespace
{
PalEntry* gPals[16384];
std::vector<ImageEntry> gImages;
u32 gTextureId;
// Enhancement state (gta2gl.ini).
bool gTrueColour; // 8-bit palette channels instead of the original's 16-bit textures
u32 gTextureEpoch; // bumped when every texture has to be converted again
std::vector<u32> gConvertBuffer;

u16 To565(u32 rgb)
{
    const u32 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (u16)(((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255));
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

void ConvertPalette(PalEntry* pPal)
{
    for (s32 i = 0; i < 256; i++)
    {
        const u32 c = pPal->mRaw[i];
        const u32 r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        const u32 a = i == 0 ? 0 : 0xFF000000u;
        if (gTrueColour) // enhancement
        {
            pPal->mRgba[kOpaque][i] = pPal->mRgba[kAlpha][i] = r | (g << 8) | (b << 16) | a;
            continue;
        }
        // 16-bit textures: bits truncated, expanded by replication when sampled.
        pPal->mRgba[kOpaque][i] = ((r & 0xF8) | (r >> 5)) | (((g & 0xFC) | (g >> 6)) << 8) | (((b & 0xF8) | (b >> 5)) << 16) | a;
        pPal->mRgba[kAlpha][i] = ((r >> 4) * 17) | (((g >> 4) * 17) << 8) | (((b >> 4) * 17) << 16) | a;
    }
}
} // namespace

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

TexData* Data(STexture* pTexture)
{
    return pTexture ? (TexData*)pTexture->field_1C_ptr : 0;
}

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
            gRenderer->UploadTexture(pData->mTex[variant], w, h, &pData->mPixels[0]);
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
            gRenderer->UploadTexture(pData->mTex[variant], slot, slot, &gConvertBuffer[0]);
        }
        else
        {
            return 0;
        }
        pData->mDirty[variant] = false;
    }
    return pData->mTex[variant];
}

void SetTrueColour(bool trueColour)
{
    if (trueColour == gTrueColour)
    {
        return;
    }
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

const ImageEntry* Image(s32 index)
{
    return index >= 0 && index < (s32)gImages.size() && gImages[index].mLoaded ? &gImages[index] : 0;
}
} // namespace gbh

using namespace gbh;

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
