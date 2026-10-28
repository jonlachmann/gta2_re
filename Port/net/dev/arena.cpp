// Port/net (docs/netcode.md, "Bots"): the server's own players, a bot brain each, with no client (--arena). For
// matches between bots without the network, headless and fast (net_check.sh's arena step); a windowed server shows
// one of them.
//
// "remote:<port>[:<teacher>]" slots are played by a trainer (Scripts/port/bots/rl_bots.py) over a local TCP connection, one round trip
// every kBotDecisionTicks for all of them. The game sends (native byte order, all 32-bit):
//   count, done, kBotFeatures, then per remote slot: slot, alive, reward since the last message, the buttons its
//   teacher would press (a scripted brain, "hunter" by default: for kickstarting), its features
// and the trainer answers each slot's buttons. With done (the last message, at --ticks) there is no answer.
#include "arena.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
const float kNear = 50; // blocks: closer than this to the nearest other player counts towards "near"
const uint32_t kRemoteHead = 4; // slot, alive, reward, teacher

// The trainer's reward: frags and deaths most, then damage, pickups and being near someone to fight
const float kFragReward = 1, kDeathReward = -1, kDamageReward = 0.01f, kPickupReward = 0.3f, kNearReward = 0.001f;
const float kApproachReward = 0.01f; // per block nearer the nearest other player (and as much off going away)
const float kCarReward = 0.3f; // the first car of each life (more in one life pay nothing: no getting in and out for it)
const float kCarFragReward = 0.5f; // on top of a frag made from a car

#if !defined(_WIN32)
int Connect(int port)
{
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s < 0 || connect(s, (sockaddr*)&a, sizeof(a)) != 0)
    {
        return -1;
    }
    const int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); // a round trip per decision
    return s;
}

bool SendAll(int s, const void* data, size_t n)
{
    for (const char* p = (const char*)data; n;)
    {
        const ssize_t k = send(s, p, n, 0);
        if (k <= 0)
        {
            return false;
        }
        p += k, n -= (size_t)k;
    }
    return true;
}

bool ReceiveAll(int s, void* data, size_t n)
{
    for (char* p = (char*)data; n;)
    {
        const ssize_t k = recv(s, p, n, 0);
        if (k <= 0)
        {
            return false;
        }
        p += k, n -= (size_t)k;
    }
    return true;
}
#else
// ponytail: training runs on macOS and Linux; Windows has no remote slots
int Connect(int)
{
    return -1;
}
bool SendAll(int, const void*, size_t)
{
    return false;
}
bool ReceiveAll(int, void*, size_t)
{
    return false;
}
#endif
} // namespace

bool net::Arena::Start(const std::vector<std::string>& brains, std::string* error)
{
    for (const std::string& kind : brains)
    {
        mBots.push_back(Bot());
        Bot& bot = mBots.back();
        bot.kind = kind;
        if (kind.compare(0, 7, "remote:") == 0)
        {
            if (mSocket < 0 && (mSocket = Connect(atoi(kind.c_str() + 7))) < 0)
            {
                *error = "no trainer at " + kind;
                return false;
            }
            const size_t colon = kind.find(':', 7);
            bot.teacher = CreateBotBrain(colon == std::string::npos ? "hunter" : kind.substr(colon + 1));
            if (!bot.teacher)
            {
                *error = "no teacher brain in " + kind;
                return false;
            }
            continue;
        }
        bot.brain = CreateBotBrain(kind);
        if (!bot.brain)
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
    Score(world);
    std::vector<float> message;
    for (size_t i = 0; i < mBots.size(); i++)
    {
        Bot& bot = mBots[i];
        if (bot.brain || tick % kBotDecisionTicks == 0)
        {
            FillBotView(mView, world, (int)i, tick, mNavMap.get());
        }
        if (bot.brain)
        {
            bot.buttons = bot.brain->Think(mView);
        }
        else if (tick % kBotDecisionTicks == 0)
        {
            AddRemote((int)i, message);
        }
    }
    if (!message.empty() && !Exchange(message, false))
    {
        fprintf(stderr, "net: the trainer is gone\n");
        _Exit(1); // not exit(): the game's shutdown from here never ended, and the orphans took the trainer's CPU
    }
}

// A remote slot's part of the message: slot, alive, reward, teacher, features (as floats; the ints as their bits)
void net::Arena::AddRemote(int slot, std::vector<float>& message)
{
    Bot& bot = mBots[slot];
    const bool alive = mView.hasBody && mView.health > 0;
    const uint32_t head[2] = {(uint32_t)slot, alive}, teacher = alive ? bot.teacher->Think(mView) : 0;
    message.resize(message.size() + 2);
    memcpy(&message[message.size() - 2], head, sizeof(head));
    message.push_back(bot.reward);
    message.push_back(0);
    memcpy(&message.back(), &teacher, sizeof(teacher));
    bot.reward = 0;
    message.resize(message.size() + kBotFeatures, 0.0f);
    if (alive)
    {
        BotFeatures(mView, bot.memory, &message[message.size() - kBotFeatures]);
    }
    else
    {
        bot.memory = BotMemory();
    }
}

bool net::Arena::Exchange(const std::vector<float>& message, bool done)
{
    const uint32_t per = kRemoteHead + kBotFeatures, count = (uint32_t)(message.size() / per);
    const uint32_t head[3] = {count, done, (uint32_t)kBotFeatures};
    std::vector<uint32_t> buttons(count);
    if (!SendAll(mSocket, head, sizeof(head)) || !SendAll(mSocket, message.data(), message.size() * sizeof(float)))
    {
        return false;
    }
    if (done)
    {
        return true;
    }
    if (!ReceiveAll(mSocket, buttons.data(), count * sizeof(uint32_t)))
    {
        return false;
    }
    for (uint32_t k = 0; k < count; k++)
    {
        uint32_t slot = 0;
        memcpy(&slot, &message[k * per], sizeof(slot));
        mBots[slot].buttons = buttons[k];
    }
    return true;
}

// Per tick: the trainer's rewards, and for the result line how close each bot kept to the nearest other player, the
// health others lost while it faced them, the cars it got into and the pickups it took
void net::Arena::Score(const Snapshot& world)
{
    std::vector<Bot::Body> bodies(mBots.size());
    for (const EntityState& e : world.entities)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(e, &p, &b) && p < (int)bodies.size())
        {
            Bot::Body& body = bodies[p];
            body.x = engine::Blocks(e.f[0]), body.y = engine::Blocks(e.f[1]), body.rot = e.f[3];
            body.health = engine::PlayerHealth(e), body.frags = engine::PlayerFrags(e), body.deaths = engine::PlayerDeaths(e);
            body.car = engine::PlayerCarId(e);
            for (int w = 0; w < 15; w++)
            {
                body.guns += engine::PlayerAmmo(e, w) > 0;
            }
        }
    }
    for (size_t i = 0; i < mBots.size(); i++)
    {
        const Bot::Body& me = bodies[i];
        Bot& bot = mBots[i];
        float nearest = 1e9f, shooterDist = 1e9f;
        int shooter = -1;
        for (size_t j = 0; j < mBots.size(); j++)
        {
            const Bot::Body& other = bodies[j];
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
            bot.closeness += 1 - nearest / kNear;
            bot.reward += kNearReward * (1 - nearest / kNear);
            const float closer = bot.last.nearest - nearest; // ponytail: the nearest one may be another than last tick's
            bot.reward += bot.last.health > 0 && fabsf(closer) < 1 ? kApproachReward * closer : 0;
        }
        const int lost = bot.last.health - me.health;
        if (lost > 0 && shooter >= 0 && bot.last.health > 0)
        {
            mBots[shooter].dealt += lost;
            mBots[shooter].reward += kDamageReward * lost;
        }
        if (mTicks > 0)
        {
            bot.reward += kFragReward * (me.frags - bot.last.frags) + kDeathReward * (me.deaths - bot.last.deaths);
            bot.reward += me.car || bot.last.car ? kCarFragReward * (me.frags - bot.last.frags) : 0;
            bot.droveThisLife &= me.deaths == bot.last.deaths;
            if (me.car && !bot.droveThisLife)
            {
                bot.droveThisLife = true;
                bot.reward += kCarReward;
            }
            // A new kind of weapon (more ammo for one it has isn't counted: GTA2_NET_TESTARM refills its own)
            const bool took = me.guns > bot.last.guns && me.health > 0 && bot.last.health > 0;
            bot.pickups += took;
            bot.reward += took ? kPickupReward : 0;
            bot.cars += me.car && !bot.last.car;
        }
        bot.last = me;
        bot.last.nearest = nearest;
    }
    mTicks++;
}

uint32_t net::Arena::Buttons(int slot) const
{
    return mBots[slot].buttons;
}

void net::Arena::Finish(const Snapshot& world)
{
    for (const EntityState& e : world.entities)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(e, &p, &b) && p < (int)mBots.size())
        {
            const Bot& bot = mBots[p];
            printf("net: arena %d %s frags %d deaths %d near %.3f dealt %d cars %d pickups %d\n", p, bot.kind.c_str(), engine::PlayerFrags(e),
                   engine::PlayerDeaths(e), mTicks ? bot.closeness / mTicks : 0.0, bot.dealt, bot.cars, bot.pickups);
        }
    }
    if (mSocket >= 0)
    {
        std::vector<float> message;
        for (size_t i = 0; i < mBots.size(); i++)
        {
            if (!mBots[i].brain)
            {
                mView = BotView(); // its last reward; no features
                AddRemote((int)i, message);
            }
        }
        Exchange(message, true);
    }
}
