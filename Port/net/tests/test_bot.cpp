// Port/net bot navigation test (Scripts/port/net_tests.sh): FindPath on made-up maps, no game.
#include "../session/bot.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) \
    do \
    { \
        if (!(c)) \
        { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            exit(1); \
        } \
    } while (0)

namespace
{
const int N = net::engine::NavMap::kSize;

// Every cell walkable at height 2, every way open, except what <blocked> closes (both sides of each wall)
void Open(net::engine::NavMap& m)
{
    memset(m.height, 2, sizeof(m.height));
    for (int y = 0; y < N; y++)
    {
        for (int x = 0; x < N; x++)
        {
            uint8_t w = 0;
            w |= y > 0 ? net::engine::NavMap::kNorth : 0;
            w |= y < N - 1 ? net::engine::NavMap::kSouth : 0;
            w |= x > 0 ? net::engine::NavMap::kWest : 0;
            w |= x < N - 1 ? net::engine::NavMap::kEast : 0;
            m.ways[y * N + x] = w;
        }
    }
}

// A wall between x and x + 1 for y in [y0, y1]
void WallEast(net::engine::NavMap& m, int x, int y0, int y1)
{
    for (int y = y0; y <= y1; y++)
    {
        m.ways[y * N + x] &= ~net::engine::NavMap::kEast;
        m.ways[y * N + x + 1] &= ~net::engine::NavMap::kWest;
    }
}

bool Valid(const net::engine::NavMap& m, int sx, int sy, const std::vector<int>& path)
{
    int c = sy * N + sx;
    for (size_t i = 0; i < path.size(); i++)
    {
        const int dx = path[i] % N - c % N, dy = path[i] / N - c / N;
        const uint8_t way = dx == 1 ? net::engine::NavMap::kEast : dx == -1 ? net::engine::NavMap::kWest
                          : dy == 1 ? net::engine::NavMap::kSouth : dy == -1 ? net::engine::NavMap::kNorth : 0;
        if (!way || (dx && dy) || !(m.ways[c] & way))
        {
            return false;
        }
        c = path[i];
    }
    return true;
}
} // namespace

int main()
{
    static net::engine::NavMap m;
    Open(m);
    std::vector<int> path;
    CHECK(net::FindPath(m, 10, 10, 20, 15, path));
    CHECK(path.size() == 15 && path.back() == 15 * N + 20 && Valid(m, 10, 10, path)); // the shortest way: 10 + 5 steps

    // A wall from y 0 to 200 at x 50/51, a gap at y 100: the way goes through the gap
    WallEast(m, 50, 0, 99);
    WallEast(m, 50, 101, 200);
    CHECK(net::FindPath(m, 40, 20, 60, 20, path));
    CHECK(Valid(m, 40, 20, path) && path.back() == 20 * N + 60);
    bool throughGap = false;
    for (size_t i = 0; i < path.size(); i++)
    {
        throughGap |= path[i] == 100 * N + 51;
    }
    CHECK(throughGap && path.size() == 20 + 80 * 2);

    // The gap closed, and the wall down to the edge: no way within reach
    WallEast(m, 50, 100, 100);
    WallEast(m, 50, 201, N - 1);
    CHECK(!net::FindPath(m, 40, 20, 60, 20, path));
    CHECK(net::FindPath(m, 40, 20, 40, 20, path) && path.empty()); // already there

    printf("net bot tests passed\n");
    return 0;
}
