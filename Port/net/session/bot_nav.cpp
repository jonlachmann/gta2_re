// Port/net: A* on the bot's map (bot.hpp). Four ways per cell, each step costs 1, Manhattan distance as the
// estimate. No game code: tested by Port/net/tests/test_bot.cpp.
#include "bot.hpp"
#include <queue>

bool net::FindPath(const engine::NavMap& map, int sx, int sy, int gx, int gy, std::vector<int>& path, int maxExpand)
{
    const int N = engine::NavMap::kSize;
    path.clear();
    if (sx < 0 || sy < 0 || gx < 0 || gy < 0 || sx >= N || sy >= N || gx >= N || gy >= N)
    {
        return false;
    }
    const int start = sy * N + sx, goal = gy * N + gx;
    if (start == goal)
    {
        return true;
    }
    static int cameFrom[N * N];
    static int cost[N * N];
    static uint32_t stamp[N * N]; // which search wrote a cell: no clearing per search
    static uint32_t search = 0;
    search++;
    typedef std::pair<int, int> Open; // (estimate, cell), smallest first
    std::priority_queue<Open, std::vector<Open>, std::greater<Open> > open;
    stamp[start] = search;
    cost[start] = 0;
    cameFrom[start] = -1;
    open.push(Open(0, start));
    static const int kDx[4] = {0, 0, -1, 1}, kDy[4] = {-1, 1, 0, 0};
    static const uint8_t kWay[4] = {engine::NavMap::kNorth, engine::NavMap::kSouth, engine::NavMap::kWest, engine::NavMap::kEast};
    int expanded = 0;
    while (!open.empty() && expanded < maxExpand)
    {
        const int cell = open.top().second;
        open.pop();
        if (cell == goal)
        {
            for (int c = goal; c != start; c = cameFrom[c])
            {
                path.push_back(c);
            }
            std::vector<int>(path.rbegin(), path.rend()).swap(path);
            return true;
        }
        expanded++;
        const int x = cell % N, y = cell / N;
        for (int d = 0; d < 4; d++)
        {
            if (!(map.ways[cell] & kWay[d]))
            {
                continue;
            }
            const int next = (y + kDy[d]) * N + (x + kDx[d]);
            const int c = cost[cell] + 1;
            if (stamp[next] != search || c < cost[next])
            {
                stamp[next] = search;
                cost[next] = c;
                cameFrom[next] = cell;
                const int nx = next % N, ny = next / N;
                const int h = (nx > gx ? nx - gx : gx - nx) + (ny > gy ? ny - gy : gy - ny);
                open.push(Open(c + h, next));
            }
        }
    }
    return false;
}
