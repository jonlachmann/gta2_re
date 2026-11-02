// The mouse cursor: a sprite of the player's own game data, turned to point up-left, outlined, and
// scaled with the display; the system arrow when the data can't be read.
#include "port_platform.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <vector>

namespace
{
// The yellow objective arrow of the HUD (user sprite 5; 7 is the red one).
const char* const kStyle = "data\\wil.sty";
const int kUserSprite = 5;
const float kTurn = 35.0f; // degrees, counter-clockwise from pointing up

struct Image
{
    int mW = 0;
    int mH = 0;
    std::vector<Uint32> mPixels; // ABGR, as SDL_PIXELFORMAT_RGBA32 on little endian

    Uint32& At(int x, int y) { return mPixels[y * mW + x]; }
    Uint32 At(int x, int y) const { return mPixels[y * mW + x]; }
    bool Opaque(int x, int y) const { return x >= 0 && y >= 0 && x < mW && y < mH && (At(x, y) >> 24) >= 128; }
};

Image NewImage(int w, int h)
{
    Image img;
    img.mW = w;
    img.mH = h;
    img.mPixels.assign(static_cast<size_t>(w) * h, 0);
    return img;
}

Uint32 Rgba(int r, int g, int b, int a)
{
    return static_cast<Uint32>(r | g << 8 | b << 16 | a << 24);
}

// A GTA2 style's sprite (chunks PALX, PPAL, PALB, SPRB, SPRX, SPRG), user sprites counted from 0.
bool LoadUserSprite(const char* path, int user, Image* pOut)
{
    char hostPath[PATH_MAX];
    FILE* f = fopen(port_resolve_path(path, hostPath), "rb");
    if (!f)
    {
        return false;
    }
    std::vector<Uint8> data;
    Uint8 buffer[65536];
    for (size_t n; (n = fread(buffer, 1, sizeof(buffer), f)) > 0;)
    {
        data.insert(data.end(), buffer, buffer + n);
    }
    fclose(f);
    if (data.size() < 6 || memcmp(data.data(), "GBST", 4) != 0)
    {
        return false;
    }
    const Uint8* pChunks[6] = {};
    size_t sizes[6] = {};
    static const char* const kNames[6] = {"PALX", "PPAL", "PALB", "SPRB", "SPRX", "SPRG"};
    for (size_t pos = 6; pos + 8 <= data.size();)
    {
        Uint32 size;
        memcpy(&size, &data[pos + 4], 4);
        if (pos + 8 + size > data.size())
        {
            break;
        }
        for (int i = 0; i < 6; i++)
        {
            if (memcmp(&data[pos], kNames[i], 4) == 0)
            {
                pChunks[i] = &data[pos + 8];
                sizes[i] = size;
            }
        }
        pos += 8 + size;
    }
    for (int i = 0; i < 6; i++)
    {
        if (!pChunks[i])
        {
            return false;
        }
    }
    auto u16 = [](const Uint8* p, size_t i) { return static_cast<int>(p[i * 2] | p[i * 2 + 1] << 8); };
    const int sprite = u16(pChunks[3], 0) + u16(pChunks[3], 1) + u16(pChunks[3], 2) + u16(pChunks[3], 3) + user;
    if (static_cast<size_t>(sprite + 1) * 8 > sizes[4])
    {
        return false;
    }
    const Uint8* pEntry = pChunks[4] + sprite * 8;
    const Uint32 ptr = pEntry[0] | pEntry[1] << 8 | pEntry[2] << 16 | static_cast<Uint32>(pEntry[3]) << 24;
    const int w = pEntry[4];
    const int h = pEntry[5];
    // Each sprite has its own palette, after the tiles'.
    const size_t virtualPalette = static_cast<size_t>(u16(pChunks[2], 0) + sprite);
    if (virtualPalette * 2 + 2 > sizes[0] || ptr + (h - 1) * 256 + w > sizes[5])
    {
        return false;
    }
    const int palette = u16(pChunks[0], virtualPalette);
    *pOut = NewImage(w, h);
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const int index = pChunks[5][ptr + y * 256 + x];
            const size_t colour = (palette / 64) * 65536 + (index * 64 + palette % 64) * 4;
            if (index && colour + 4 <= sizes[1])
            {
                const Uint8* pBgra = pChunks[1] + colour;
                pOut->At(x, y) = Rgba(pBgra[2], pBgra[1], pBgra[0], 255);
            }
        }
    }
    return true;
}

// Scaled by a whole factor, then turned with 4x4 samples per pixel for smooth edges.
Image Turn(const Image& src, int scale, float degrees)
{
    const float w = static_cast<float>(src.mW * scale);
    const float h = static_cast<float>(src.mH * scale);
    const float c = cosf(degrees * 3.14159265f / 180);
    const float s = sinf(degrees * 3.14159265f / 180);
    const int outW = static_cast<int>(ceilf(fabsf(w * c) + fabsf(h * s))) + 2;
    const int outH = static_cast<int>(ceilf(fabsf(w * s) + fabsf(h * c))) + 2;
    Image out = NewImage(outW, outH);
    const int kSamples = 4;
    for (int y = 0; y < outH; y++)
    {
        for (int x = 0; x < outW; x++)
        {
            int r = 0, g = 0, b = 0, n = 0;
            for (int j = 0; j < kSamples; j++)
            {
                for (int i = 0; i < kSamples; i++)
                {
                    const float dx = x + (i + 0.5f) / kSamples - outW / 2.0f;
                    const float dy = y + (j + 0.5f) / kSamples - outH / 2.0f;
                    const float sx = dx * c - dy * s + w / 2;
                    const float sy = dx * s + dy * c + h / 2;
                    if (sx >= 0 && sy >= 0 && sx < w && sy < h)
                    {
                        const Uint32 p = src.At(static_cast<int>(sx) / scale, static_cast<int>(sy) / scale);
                        if (p >> 24)
                        {
                            r += p & 0xFF;
                            g += p >> 8 & 0xFF;
                            b += p >> 16 & 0xFF;
                            n++;
                        }
                    }
                }
            }
            if (n)
            {
                out.At(x, y) = Rgba(r / n, g / n, b / n, 255 * n / (kSamples * kSamples));
            }
        }
    }
    return out;
}

// A dark line one pixel wide around the opaque pixels, so the cursor reads on any background.
Image Outline(const Image& src)
{
    Image out = NewImage(src.mW + 2, src.mH + 2);
    for (int y = 0; y < out.mH; y++)
    {
        for (int x = 0; x < out.mW; x++)
        {
            const Uint32 p = x > 0 && y > 0 && x <= src.mW && y <= src.mH ? src.At(x - 1, y - 1) : 0;
            bool edge = false;
            for (int j = -2; j <= 0 && (p >> 24) < 128; j++)
            {
                for (int i = -2; i <= 0; i++)
                {
                    edge |= src.Opaque(x + i, y + j);
                }
            }
            out.At(x, y) = edge ? Rgba(12, 8, 8, 255) : p;
        }
    }
    return out;
}

void SetCursor(const Image& img)
{
    // The hotspot is the tip: the opaque pixel nearest the top left corner.
    int hotX = img.mW;
    int hotY = img.mH;
    for (int y = 0; y < img.mH; y++)
    {
        for (int x = 0; x < img.mW; x++)
        {
            if (img.Opaque(x, y) && x + y < hotX + hotY)
            {
                hotX = x;
                hotY = y;
            }
        }
    }
    SDL_Surface* pSurface = SDL_CreateSurfaceFrom(img.mW, img.mH, SDL_PIXELFORMAT_RGBA32,
                                                  const_cast<Uint32*>(img.mPixels.data()), img.mW * 4);
    SDL_Cursor* pCursor = pSurface ? SDL_CreateColorCursor(pSurface, hotX, hotY) : NULL;
    SDL_DestroySurface(pSurface);
    if (pCursor)
    {
        SDL_SetCursor(pCursor);
    }
}
} // namespace

// About the size of the system's arrow: the sprite's pixels as they are up to 1440 lines, one more
// step every 1080 lines from there (twice as big at 4K).
void port_SetGameCursor()
{
    Image sprite;
    if (!LoadUserSprite(kStyle, kUserSprite, &sprite))
    {
        return;
    }
    const SDL_DisplayMode* pMode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    const int scale = pMode && pMode->h > 1080 ? (pMode->h + 540) / 1080 : 1;
    Image img = Turn(sprite, scale, kTurn);
    for (int i = 0; i < scale; i++)
    {
        img = Outline(img);
    }
    if (const char* pPath = getenv("GTA2_CURSOR_BMP"))
    {
        SDL_Surface* pSurface = SDL_CreateSurfaceFrom(img.mW, img.mH, SDL_PIXELFORMAT_RGBA32, img.mPixels.data(), img.mW * 4);
        SDL_SaveBMP(pSurface, pPath);
        SDL_DestroySurface(pSurface);
    }
    SetCursor(img);
}
