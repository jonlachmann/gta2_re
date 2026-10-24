// Port/net (docs/netcode.md, "Bots"): the server's own players, a bot brain each, with no client (--arena). For
// matches between bots without the network: Scripts/port/train_bots.py runs them headless and fast and reads the
// result line; a windowed server shows one of them.
#include "session.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
const float kNear = 50; // blocks: closer than this to the nearest other player counts towards "near"
}

bool net::Arena::Start(const std::vector<std::string>& brains, std::string* error)
{
    for (const std::string& kind : brains)
    {
        mBots.push_back(Bot());
        mBots.back().kind = kind;
        mBots.back().brain = CreateBotBrain(kind);
        if (!mBots.back().brain)
        {
            *error = "no bot brain " + kind;
            return false;
        }
    }
    return true;
}

void net::Arena::Think(const Snapshot& world, uint32_t tick)
{
    if (!mNavMap)
    {
        mNavMap.reset(new engine::NavMap());
        engine::BuildNavMap(*mNavMap);
    }
    for (size_t i = 0; i < mBots.size(); i++)
    {
        FillBotView(mView, world, (int)i, tick, mNavMap.get());
        mBots[i].buttons = mBots[i].brain->Think(mView);
    }
    Score(world);
}

// What the result line has besides frags and deaths, for a trainer to shape its reward: how close each bot kept
// to the nearest other player, and the health others lost while it faced them
void net::Arena::Score(const Snapshot& world)
{
    struct Body
    {
        float x, y;
        int rot, health;
    };
    std::vector<Body> bodies(mBots.size(), Body{0, 0, 0, 0});
    for (const EntityState& e : world.entities)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(e, &p, &b) && p < (int)bodies.size())
        {
            bodies[p] = Body{engine::Blocks(e.f[0]), engine::Blocks(e.f[1]), e.f[3], engine::PlayerHealth(e)};
        }
    }
    for (size_t i = 0; i < mBots.size(); i++)
    {
        const Body& me = bodies[i];
        float nearest = 1e9f, shooterDist = 1e9f;
        int shooter = -1;
        for (size_t j = 0; j < mBots.size(); j++)
        {
            const Body& other = bodies[j];
            const float dx = me.x - other.x, dy = me.y - other.y, d = hypotf(dx, dy);
            if (j == i || other.health <= 0)
            {
                continue;
            }
            nearest = d < nearest ? d : nearest;
            // ponytail: who faces it (30 degrees, 10 blocks) or stands at it (3) hurt it; cars and fire mislead it
            int err = (int)(atan2f(dx, dy) * 720.0f / 3.14159265f) - other.rot;
            err = ((err % 1440) + 2160) % 1440 - 720;
            if ((d < 3 || (d < 10 && abs(err) < 120)) && d < shooterDist)
            {
                shooterDist = d;
                shooter = (int)j;
            }
        }
        if (me.health > 0 && nearest < kNear)
        {
            mBots[i].closeness += 1 - nearest / kNear;
        }
        const int lost = mBots[i].health - me.health;
        if (lost > 0 && me.health >= 0 && shooter >= 0 && mBots[i].health > 0)
        {
            mBots[shooter].dealt += lost;
        }
        mBots[i].health = me.health;
    }
    mTicks++;
}

uint32_t net::Arena::Buttons(int slot) const
{
    return mBots[slot].buttons;
}

void net::Arena::PrintResult(const Snapshot& world) const
{
    for (const EntityState& e : world.entities)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(e, &p, &b) && p < (int)mBots.size())
        {
            const Bot& bot = mBots[p];
            printf("net: arena %d %s frags %d deaths %d near %.3f dealt %d\n", p, bot.kind.c_str(), engine::PlayerFrags(e), engine::PlayerDeaths(e),
                   mTicks ? bot.closeness / mTicks : 0.0, bot.dealt);
        }
    }
}
