// The mouse cursor: a sprite of the player's own game data, turned to point up-left, smoothed,
// outlined and shadowed, scaled with the display; the system arrow when the data can't be read.
#include "port_platform.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <vector>

namespace
{
// The frontend's yellow arrow (wil.sty's user sprites 5 and 7, turned 35 degrees, are the HUD's
// yellow and red objective arrows).
const char* const kStyle = "data\\fstyle.sty";
const int kUserSprite = 0;
const float kTurn = 45.0f; // degrees, counter-clockwise from pointing up

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

// The sprite's opacity and colour between its pixel centres; the 0.5 contour of the opacity smooths
// the stair steps of its edges.
float Opacity(const Image& src, float x, float y)
{
    x -= 0.5f;
    y -= 0.5f;
    const int x0 = static_cast<int>(floorf(x));
    const int y0 = static_cast<int>(floorf(y));
    float v = 0;
    for (int j = 0; j < 2; j++)
    {
        for (int i = 0; i < 2; i++)
        {
            if (src.Opaque(x0 + i, y0 + j))
            {
                v += (i ? x - x0 : 1 - (x - x0)) * (j ? y - y0 : 1 - (y - y0));
            }
        }
    }
    return v;
}

void Colour(const Image& src, float x, float y, float* pRgb)
{
    x -= 0.5f;
    y -= 0.5f;
    const int x0 = static_cast<int>(floorf(x));
    const int y0 = static_cast<int>(floorf(y));
    float sum = 0;
    for (int j = 0; j < 2; j++)
    {
        for (int i = 0; i < 2; i++)
        {
            if (src.Opaque(x0 + i, y0 + j))
            {
                const float weight = (i ? x - x0 : 1 - (x - x0)) * (j ? y - y0 : 1 - (y - y0));
                const Uint32 p = src.At(x0 + i, y0 + j);
                for (int k = 0; k < 3; k++)
                {
                    pRgb[k] += (p >> (8 * k) & 0xFF) * weight;
                }
                sum += weight;
            }
        }
    }
    for (int k = 0; sum > 0 && k < 3; k++)
    {
        pRgb[k] /= sum;
    }
}

// Scaled and turned with 6x6 samples per pixel, over a dark outline and a drop shadow; the hotspot
// lands on the tip, the middle of the sprite's top row.
Image Render(const Image& src, float scale, float degrees, int* pHotX, int* pHotY)
{
    const float c = cosf(degrees * 3.14159265f / 180);
    const float s = sinf(degrees * 3.14159265f / 180);
    const int line = scale < 2.5f ? 1 : 2;
    const int pad = line * 2 + 1;
    const int w = static_cast<int>(ceilf((src.mW * fabsf(c) + src.mH * fabsf(s)) * scale)) + pad * 2;
    const int h = static_cast<int>(ceilf((src.mW * fabsf(s) + src.mH * fabsf(c)) * scale)) + pad * 2;
    const int kSamples = 6;
    std::vector<float> body(static_cast<size_t>(w) * h * 4, 0.0f);
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            float* pPixel = &body[(y * w + x) * 4];
            for (int j = 0; j < kSamples; j++)
            {
                for (int i = 0; i < kSamples; i++)
                {
                    const float dx = (x + (i + 0.5f) / kSamples - w / 2.0f) / scale;
                    const float dy = (y + (j + 0.5f) / kSamples - h / 2.0f) / scale;
                    const float sx = dx * c - dy * s + src.mW / 2.0f;
                    const float sy = dx * s + dy * c + src.mH / 2.0f;
                    if (Opacity(src, sx, sy) >= 0.5f)
                    {
                        float rgb[3] = {};
                        Colour(src, sx, sy, rgb);
                        for (int k = 0; k < 3; k++)
                        {
                            pPixel[k] += rgb[k];
                        }
                        pPixel[3] += 1;
                    }
                }
            }
            for (int k = 0; pPixel[3] > 0 && k < 3; k++)
            {
                pPixel[k] /= pPixel[3];
            }
            pPixel[3] /= kSamples * kSamples;
        }
    }
    auto coverage = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h ? body[(y * w + x) * 4 + 3] : 0.0f; };
    auto outline = [&](int x, int y) {
        float v = 0;
        for (int j = -line; j <= line; j++)
        {
            for (int i = -line; i <= line; i++)
            {
                v = coverage(x + i, y + j) > v ? coverage(x + i, y + j) : v;
            }
        }
        return v;
    };
    Image out = NewImage(w, h);
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const float layers[3][4] = {{0, 0, 0, outline(x - line, y - line) * 0.45f},
                                        {14, 10, 6, outline(x, y)},
                                        {body[(y * w + x) * 4], body[(y * w + x) * 4 + 1], body[(y * w + x) * 4 + 2], coverage(x, y)}};
            float rgb[3] = {};
            float alpha = 0;
            for (const auto& layer : layers)
            {
                for (int k = 0; k < 3; k++)
                {
                    rgb[k] = layer[k] * layer[3] + rgb[k] * (1 - layer[3]);
                }
                alpha = layer[3] + alpha * (1 - layer[3]);
            }
            if (alpha > 0)
            {
                out.At(x, y) = Rgba(static_cast<int>(rgb[0] / alpha), static_cast<int>(rgb[1] / alpha),
                                    static_cast<int>(rgb[2] / alpha), static_cast<int>(alpha * 255 + 0.5f));
            }
        }
    }

    int first = src.mW;
    int last = -1;
    for (int x = 0; x < src.mW; x++)
    {
        if (src.Opaque(x, 0))
        {
            first = x < first ? x : first;
            last = x;
        }
    }
    const float tipX = (first + last + 1) / 2.0f - src.mW / 2.0f;
    const float tipY = -src.mH / 2.0f;
    *pHotX = static_cast<int>((tipX * c + tipY * s) * scale + w / 2.0f);
    *pHotY = static_cast<int>((-tipX * s + tipY * c) * scale + h / 2.0f);
    return out;
}
} // namespace

// About the size of the system's arrow: one and a half sprite pixels per screen pixel at 1080 lines,
// in proportion to the display's height.
void port_SetGameCursor()
{
    Image sprite;
    if (!LoadUserSprite(kStyle, kUserSprite, &sprite))
    {
        return;
    }
    // Only the sprite's opaque part, which must start on its top row.
    int right = 0;
    int bottom = 0;
    for (int y = 0; y < sprite.mH; y++)
    {
        for (int x = 0; x < sprite.mW; x++)
        {
            if (sprite.Opaque(x, y))
            {
                right = x + 1 > right ? x + 1 : right;
                bottom = y + 1;
            }
        }
    }
    Image cropped = NewImage(right, bottom);
    for (int y = 0; y < bottom; y++)
    {
        for (int x = 0; x < right; x++)
        {
            cropped.At(x, y) = sprite.At(x, y);
        }
    }
    const SDL_DisplayMode* pMode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    const float scale = pMode && pMode->h > 0 ? pMode->h / 720.0f : 1.5f;
    int hotX;
    int hotY;
    const Image img = Render(cropped, scale, kTurn, &hotX, &hotY);
    if (const char* pPath = getenv("GTA2_CURSOR_BMP"))
    {
        SDL_Surface* pSurface = SDL_CreateSurfaceFrom(img.mW, img.mH, SDL_PIXELFORMAT_RGBA32,
                                                      const_cast<Uint32*>(img.mPixels.data()), img.mW * 4);
        SDL_SaveBMP(pSurface, pPath);
        SDL_DestroySurface(pSurface);
        SDL_Log("cursor %dx%d, hotspot %d,%d", img.mW, img.mH, hotX, hotY);
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
