// Port/net (docs/netcode.md, "Bots"): the map's walkable cells for bots. A cell is its column's highest block (road,
// pavement or field); a walker may step to a neighbour of the same height, or one level up or down where either
// side is a slope, unless a wall face is in the way on the walking level or the ground block (the faces the
// game's own collision tests: left/right for x, top/bottom for y).
#include "internal.hpp"
#include <stdio.h>
#include "Source/block_face.hpp"
#include "Source/map_0x370.hpp"
#include <string.h>

namespace
{
const int N = net::engine::NavMap::kSize;

bool Walled(s32 x, s32 y, s32 z, int dx, int dy)
{
    // Leaving (x, y) towards (x + dx, y + dy): this block's face on that side, the next block's on ours
    for (s32 level = z; level <= z + 1; level++)
    {
        gmp_block_info* here = gMap_0x370_6F6268->GetBlock_4DFE10(x, y, level);
        gmp_block_info* there = gMap_0x370_6F6268->GetBlock_4DFE10(x + dx, y + dy, level);
        const u16 out = !here ? 0 : dx > 0 ? here->field_2_right : dx < 0 ? here->field_0_left : dy > 0 ? here->field_6_bottom : here->field_4_top;
        const u16 in = !there ? 0 : dx > 0 ? there->field_0_left : dx < 0 ? there->field_2_right : dy > 0 ? there->field_4_top : there->field_6_bottom;
        if ((out | in) & block_face::wall)
        {
            return true;
        }
    }
    return false;
}
} // namespace

void net::engine::BuildNavMap(NavMap& out)
{
    static bool sSlope[N * N];
    for (int y = 0; y < N; y++)
    {
        for (int x = 0; x < N; x++)
        {
            s32 z = 0;
            gmp_block_info* top = gMap_0x370_6F6268 ? gMap_0x370_6F6268->FindHighestBlockForCoord_4E4C30(x, y, &z) : NULL;
            const bool ground = top && !IS_AIR_TYPE(top->field_B_slope_type) && z >= 0 && z < 255;
            out.height[y * N + x] = ground ? (uint8_t)z : 255;
            sSlope[y * N + x] = ground && IS_GRADIENT_SLOPE(top->field_B_slope_type);
        }
    }
    static const int kDx[4] = {0, 0, -1, 1}, kDy[4] = {-1, 1, 0, 0};
    static const uint8_t kWay[4] = {NavMap::kNorth, NavMap::kSouth, NavMap::kWest, NavMap::kEast};
    for (int y = 0; y < N; y++)
    {
        for (int x = 0; x < N; x++)
        {
            uint8_t ways = 0;
            const int h = out.height[y * N + x];
            for (int d = 0; d < 4 && h != 255; d++)
            {
                const int nx = x + kDx[d], ny = y + kDy[d];
                if (nx < 0 || ny < 0 || nx >= N || ny >= N || out.height[ny * N + nx] == 255)
                {
                    continue;
                }
                const int nh = out.height[ny * N + nx];
                const int step = nh > h ? nh - h : h - nh;
                if (step > 1 || (step == 1 && !sSlope[y * N + x] && !sSlope[ny * N + nx]))
                {
                    continue;
                }
                if (!Walled(x, y, h < nh ? h : nh, kDx[d], kDy[d]))
                {
                    ways |= kWay[d];
                }
            }
            out.ways[y * N + x] = ways;
        }
    }
}

void net::engine::DumpNavMap(const NavMap& map, const char* path)
{
    if (FILE* f = fopen(path, "wb"))
    {
        fprintf(f, "P5 %d %d 255\n", NavMap::kSize, NavMap::kSize);
        for (int c = 0; c < NavMap::kSize * NavMap::kSize; c++)
        {
            const int h = map.height[c], w = map.ways[c];
            fputc(h == 255 || !w ? 0 : 60 + h * 24 + (w == 15 ? 20 : 0), f);
        }
        fclose(f);
    }
}
