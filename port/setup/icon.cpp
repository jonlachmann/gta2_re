// The window icon: GTA2's own, from the player's gta2.exe (resource group 0x65, as the game's
// LoadIconA asks for), saved by the setup as gta2_icon.bmp.
#include "install.hpp"
#include "setup.hpp"

#include <SDL3/SDL.h>
#include <string.h>

namespace setup
{
namespace
{
uint32_t Le32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t Le16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

class PeResources
{
public:
    explicit PeResources(const std::vector<uint8_t>& d) : mD(d)
    {
        const uint32_t pe = Size(0x40) ? Le32(&d[0x3C]) : 0;
        if (!Size(pe + 0x100) || memcmp(&d[pe], "PE\0\0", 4) != 0)
        {
            return;
        }
        mSections = pe + 24 + Le16(&d[pe + 20]);
        mCount = Le16(&d[pe + 6]);
        const uint32_t dirs = pe + 24 + (Le16(&d[pe + 24]) == 0x20B ? 112 : 96);
        mBase = Offset(Le32(&d[dirs + 2 * 8]));
    }

    // The data of a resource (any language), or NULL.
    const uint8_t* Find(uint32_t type, uint32_t id, uint32_t* pSize)
    {
        uint32_t e = Child(mBase, type);
        e = e ? Child(mBase + (e & 0x7FFFFFFF), id) : 0;
        e = e ? Child(mBase + (e & 0x7FFFFFFF), ~0u) : 0;
        if (!e || (e & 0x80000000) || !Size(mBase + e + 8))
        {
            return NULL;
        }
        const uint32_t off = Offset(Le32(&mD[mBase + e])), size = Le32(&mD[mBase + e + 4]);
        *pSize = size;
        return off && Size(off + (uint64_t)size) ? &mD[off] : NULL;
    }

private:
    bool Size(uint64_t n) const { return n <= mD.size(); }

    uint32_t Offset(uint32_t rva) const
    {
        for (uint32_t i = 0; i < mCount && Size(mSections + 40 * (i + 1)); i++)
        {
            const uint8_t* s = &mD[mSections + 40 * i];
            const uint32_t va = Le32(s + 12), size = std::max(Le32(s + 8), Le32(s + 16));
            if (rva >= va && rva < va + size)
            {
                return Le32(s + 20) + rva - va;
            }
        }
        return 0;
    }

    // The entry of a resource directory with that id (~0: the first one), 0 if none.
    uint32_t Child(uint32_t dir, uint32_t id) const
    {
        if (!mBase || !Size(dir + 16))
        {
            return 0;
        }
        const unsigned count = Le16(&mD[dir + 12]) + Le16(&mD[dir + 14]);
        for (unsigned i = 0; i < count && Size(dir + 16 + 8 * (i + 1)); i++)
        {
            const uint8_t* e = &mD[dir + 16 + 8 * i];
            if (id == ~0u || Le32(e) == id)
            {
                return Le32(e + 4);
            }
        }
        return 0;
    }

    const std::vector<uint8_t>& mD;
    uint32_t mSections = 0, mCount = 0, mBase = 0;
};

// An icon image: a bitmap of 1 to 32 bits per pixel then its 1-bit transparency mask.
SDL_Surface* DecodeIcon(const uint8_t* p, uint32_t size)
{
    if (size < 40 || Le32(p) < 40)
    {
        return NULL;
    }
    const int w = (int)Le32(p + 4), h = (int)Le32(p + 8) / 2, bpp = Le16(p + 14);
    const uint32_t colors = bpp <= 8 ? (Le32(p + 32) ? Le32(p + 32) : 1u << bpp) : 0;
    const uint8_t* palette = p + Le32(p);
    const uint8_t* pixels = palette + 4 * colors;
    const size_t stride = ((size_t)w * bpp + 31) / 32 * 4, maskStride = ((size_t)w + 31) / 32 * 4;
    const uint8_t* mask = pixels + stride * h;
    if (w <= 0 || h <= 0 || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) ||
        mask + maskStride * h > p + size)
    {
        return NULL;
    }
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    for (int y = 0; s && y < h; y++)
    {
        const uint8_t* row = pixels + stride * (h - 1 - y);
        uint8_t* out = static_cast<uint8_t*>(s->pixels) + s->pitch * y;
        for (int x = 0; x < w; x++, out += 4)
        {
            const uint8_t* c = row + x * bpp / 8;
            if (bpp <= 8)
            {
                const unsigned index = (row[x * bpp / 8] >> (8 - bpp - (x * bpp) % 8)) & ((1 << bpp) - 1);
                c = palette + 4 * (index < colors ? index : 0);
            }
            const bool masked = (mask[maskStride * (h - 1 - y) + x / 8] >> (7 - x % 8)) & 1;
            out[0] = c[2];
            out[1] = c[1];
            out[2] = c[0];
            out[3] = bpp == 32 ? c[3] : (masked ? 0 : 255);
        }
    }
    return s;
}

// The largest image of gta2.exe's icon.
SDL_Surface* LoadExeIcon(const fs::path& exe)
{
    FILE* f = OpenFile(exe, "rb");
    if (!f)
    {
        return NULL;
    }
    std::vector<uint8_t> d;
    uint8_t buf[1 << 16];
    for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0 && d.size() < (16u << 20);)
    {
        d.insert(d.end(), buf, buf + n);
    }
    fclose(f);
    PeResources res(d);
    uint32_t size;
    const uint8_t* group = res.Find(14, 0x65, &size); // RT_GROUP_ICON
    if (!group || size < 6)
    {
        return NULL;
    }
    unsigned best = 0, bestId = 0;
    for (unsigned i = 0; i < Le16(group + 4) && 6 + 14 * (i + 1) <= size; i++)
    {
        const uint8_t* e = group + 6 + 14 * i;
        const unsigned score = ((e[0] ? e[0] : 256) * (e[1] ? e[1] : 256)) * 64 + Le16(e + 6);
        if (score > best)
        {
            best = score;
            bestId = Le16(e + 12);
        }
    }
    const uint8_t* image = res.Find(3, bestId, &size); // RT_ICON
    return image ? DecodeIcon(image, size) : NULL;
}
} // namespace

bool SaveExeIcon(const fs::path& exe, const fs::path& bmp)
{
    SDL_Surface* s = LoadExeIcon(exe);
    const bool ok = s && SDL_SaveBMP(s, PathToUtf8(bmp).c_str());
    SDL_DestroySurface(s);
    return ok;
}
} // namespace setup

void port_SetWindowIcon(SDL_Window* pWindow)
{
    SDL_Surface* s = SDL_LoadBMP("gta2_icon.bmp");
    if (!s)
    {
        setup::fs::path exe;
        s = setup::FindNoCase(".", "gta2.exe", &exe) ? setup::LoadExeIcon(exe) : NULL;
    }
    if (s)
    {
        SDL_SetWindowIcon(pWindow, s);
        if (const char* path = SDL_getenv("GTA2_ICON_BMP"))
        {
            SDL_SaveBMP(s, path);
        }
        SDL_DestroySurface(s);
    }
}
