// Port/net: the network bots' features and actions (bot.hpp), and the "nn:<file>" brain: a multilayer perceptron
// from Scripts/port/bots/rl_bots.py (PPO, with torch). The file:
//   mlp <argmax|sample> <layers> <inputs> <size>... <kBotLogits>
// then per layer, per output, its weights for each input and a bias. Hidden layers are tanh.
#include "bot.hpp"
#include <algorithm>
#include <math.h>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
const float kPi = 3.14159265f;
const int kPlayers = 3, kCars = 2, kPeds = 1, kWeapons = 2, kPowerUps = 1; // the nearest ones it sees
const int kRays = 8; // how far it can walk, in 8 directions
const float kRange = 16; // blocks: farther is "far"
const uint32_t kActionBits[8] = {net::kBtnLeft, net::kBtnRight, net::kBtnForward, net::kBtnBack, net::kBtnAttack, net::kBtnEnterExit, net::kBtnJump, net::kBtnNextWeapon};

float Clamp(float v)
{
    return v < -1 ? -1 : v > 1 ? 1 : v;
}

// Into the bot's own frame: ahead, to its left, and the distance, in kRange
void Relative(const net::BotView& v, float x, float y, float* out)
{
    // Facing rot walks along (sin, cos) (bot.hpp); its left is towards larger angles
    const float a = v.rot * kPi / 720.0f, dx = x - v.x, dy = y - v.y;
    out[0] = Clamp((dx * sinf(a) + dy * cosf(a)) / kRange);
    out[1] = Clamp((dx * cosf(a) - dy * sinf(a)) / kRange);
    out[2] = Clamp(hypotf(dx, dy) / kRange);
}

// The <n> nearest of a kind (pickups: weapons or power-ups), 3 values each (and a player's health): none is far ahead
float* Nearest(const net::BotView& v, net::BotView::Seen::Kind kind, int n, float* out, bool weapons = false)
{
    std::vector<std::pair<float, const net::BotView::Seen*>> sorted;
    for (const net::BotView::Seen& s : v.seen)
    {
        if (s.kind == kind && (kind != net::BotView::Seen::kPlayer || s.health > 0) && (kind != net::BotView::Seen::kPickup || s.weapon == weapons))
        {
            sorted.push_back(std::make_pair(hypotf(s.x - v.x, s.y - v.y), &s));
        }
    }
    std::sort(sorted.begin(), sorted.end(), [](const std::pair<float, const net::BotView::Seen*>& a, const std::pair<float, const net::BotView::Seen*>& b) {
        return a.first < b.first;
    });
    for (int i = 0; i < n; i++)
    {
        const bool seen = i < (int)sorted.size();
        if (seen)
        {
            Relative(v, sorted[i].second->x, sorted[i].second->y, out);
        }
        else
        {
            out[0] = 1, out[1] = 0, out[2] = 1;
        }
        out += 3;
        if (kind == net::BotView::Seen::kPlayer)
        {
            *out++ = seen ? sorted[i].second->health / 100.0f : 0;
        }
    }
    return out;
}

// How far it can walk from its cell in <dir> (0..7, a turn of 45 degrees each from ahead), 0..1 of 6 cells
float Ray(const net::BotView& v, int dir)
{
    if (!v.map)
    {
        return 1;
    }
    const int N = net::engine::NavMap::kSize;
    const float a = v.rot * kPi / 720.0f + dir * kPi / 4, sx = sinf(a), sy = cosf(a);
    // A step to the next cell: along x, then y (a diagonal one through either)
    const auto canGo = [&](int x, int y, int tx, int ty) {
        const uint8_t w = v.map->ways[y * N + x];
        return tx > x   ? (w & net::engine::NavMap::kEast) != 0
               : tx < x ? (w & net::engine::NavMap::kWest) != 0
               : ty > y ? (w & net::engine::NavMap::kSouth) != 0
               : ty < y ? (w & net::engine::NavMap::kNorth) != 0
                        : true;
    };
    int cx = (int)v.x, cy = (int)v.y;
    for (float t = 0.5f; t <= 6; t += 0.5f)
    {
        const int nx = (int)(v.x + sx * t), ny = (int)(v.y + sy * t);
        if (nx < 0 || ny < 0 || nx >= N || ny >= N ||
            ((nx != cx || ny != cy) && !((canGo(cx, cy, nx, cy) && canGo(nx, cy, nx, ny)) || (canGo(cx, cy, cx, ny) && canGo(cx, ny, nx, ny)))))
        {
            return t / 6;
        }
        cx = nx, cy = ny;
    }
    return 1;
}

const net::BotView::Seen* NearestOf(const net::BotView& v, net::BotView::Seen::Kind kind, bool weapons)
{
    const net::BotView::Seen* best = NULL;
    float dist = 1e9f;
    for (const net::BotView::Seen& s : v.seen)
    {
        const float d = hypotf(s.x - v.x, s.y - v.y);
        if (s.kind == kind && d < dist && (kind != net::BotView::Seen::kPlayer || s.health > 0) && (kind != net::BotView::Seen::kPickup || s.weapon == weapons))
        {
            dist = d;
            best = &s;
        }
    }
    return best;
}

// Which way its route to <goal> goes next (ahead, left: a unit vector) and how long it is (0..1 of 2 * kRange
// cells); no goal: far ahead
float* Compass(const net::BotView& v, net::BotRoute& route, const net::BotView::Seen* goal, float* out)
{
    if (!goal)
    {
        out[0] = 1, out[1] = 0, out[2] = 1;
        return out + 3;
    }
    float wx = 0, wy = 0;
    route.Next(v, goal->x, goal->y, &wx, &wy);
    Relative(v, wx, wy, out);
    const float len = hypotf(out[0], out[1]);
    out[0] = len > 0 ? out[0] / len : 1, out[1] = len > 0 ? out[1] / len : 0; // the way, not how far the waypoint is
    out[2] = Clamp(route.path.size() / (2 * kRange));
    return out + 3;
}

class NeuralBot : public net::BotBrain
{
  public:
    bool Load(const char* path)
    {
        FILE* f = fopen(path, "r");
        if (!f)
        {
            return false;
        }
        char mode[16] = {};
        int layers = 0;
        bool ok = fscanf(f, "mlp %15s %d", mode, &layers) == 2 && layers >= 1 && layers <= 8;
        mSample = !strcmp(mode, "sample");
        mSizes.assign(ok ? layers + 1 : 0, 0);
        for (int& n : mSizes)
        {
            ok = ok && fscanf(f, "%d", &n) == 1 && n > 0 && n <= 1024;
        }
        // Fewer inputs: a network from before features were added at the end, which it doesn't read
        ok = ok && mSizes.front() <= net::kBotFeatures && mSizes.back() == net::kBotLogits;
        for (int l = 0; ok && l < layers; l++)
        {
            mWeights.push_back(std::vector<float>((mSizes[l] + 1) * mSizes[l + 1]));
            for (float& w : mWeights.back())
            {
                ok = ok && fscanf(f, "%f", &w) == 1;
            }
        }
        fclose(f);
        if (!ok)
        {
            fprintf(stderr, "net: %s is no network of up to %d inputs and %d outputs (mlp <argmax|sample> <layers> <sizes> <weights>)\n", path,
                    net::kBotFeatures, net::kBotLogits);
        }
        return ok;
    }

    uint32_t Think(const net::BotView& v) override
    {
        if (!v.hasBody || v.health <= 0)
        {
            mMemory = net::BotMemory();
            return 0;
        }
        if (v.frame % net::kBotDecisionTicks != 0)
        {
            return mMemory.lastButtons; // held, as in training
        }
        std::vector<float> in(net::kBotFeatures), out;
        net::BotFeatures(v, mMemory, in.data());
        for (size_t l = 0; l < mWeights.size(); l++)
        {
            const int n = mSizes[l], m = mSizes[l + 1];
            out.assign(m, 0);
            for (int o = 0; o < m; o++)
            {
                const float* w = &mWeights[l][o * (n + 1)];
                float sum = w[n];
                for (int i = 0; i < n; i++)
                {
                    sum += w[i] * in[i];
                }
                out[o] = l + 1 < mWeights.size() ? tanhf(sum) : sum;
            }
            in.swap(out);
        }
        int choice[net::kBotGroups];
        const float* logits = in.data();
        for (int g = 0; g < net::kBotGroups; logits += net::kBotGroupSize[g++])
        {
            choice[g] = mSample ? Sample(logits, net::kBotGroupSize[g]) : (int)(std::max_element(logits, logits + net::kBotGroupSize[g]) - logits);
        }
        mMemory.lastButtons = net::BotButtons(choice);
        return mMemory.lastButtons;
    }

    std::string Describe() const override
    {
        char line[64];
        snprintf(line, sizeof(line), "buttons 0x%x", mMemory.lastButtons);
        return line;
    }

  private:
    int Sample(const float* logits, int n)
    {
        const float top = *std::max_element(logits, logits + n);
        float p[4], sum = 0;
        for (int i = 0; i < n; i++)
        {
            sum += p[i] = expf(logits[i] - top);
        }
        float r = std::uniform_real_distribution<float>(0, sum)(mRandom);
        for (int i = 0; i < n - 1; i++)
        {
            if ((r -= p[i]) < 0)
            {
                return i;
            }
        }
        return n - 1;
    }

    std::vector<int> mSizes;
    std::vector<std::vector<float>> mWeights;
    bool mSample = false;
    std::mt19937 mRandom{std::random_device{}()}; // its own: the game's draws stay the game's
    net::BotMemory mMemory;
};
} // namespace

void net::BotFeatures(const BotView& v, BotMemory& m, float* out)
{
    float* p = out;
    *p++ = v.health / 100.0f;
    *p++ = v.car != 0;
    *p++ = m.lastX < 0 ? 0 : Clamp(hypotf(v.x - m.lastX, v.y - m.lastY) * 4); // its speed since the last call
    *p++ = v.weapon >= 0;
    *p++ = Clamp(v.ammo / 99.0f);
    *p++ = Clamp(v.guns / 4.0f);
    m.lastX = v.x, m.lastY = v.y;
    p = Nearest(v, BotView::Seen::kPlayer, kPlayers, p);
    p = Nearest(v, BotView::Seen::kCar, kCars, p);
    p = Nearest(v, BotView::Seen::kPed, kPeds, p);
    p = Nearest(v, BotView::Seen::kPickup, kWeapons, p, true);
    p = Nearest(v, BotView::Seen::kPickup, kPowerUps, p, false);
    for (int r = 0; r < kRays; r++)
    {
        *p++ = Ray(v, r);
    }
    p = Compass(v, m.toPlayer, NearestOf(v, BotView::Seen::kPlayer, false), p);
    p = Compass(v, m.toWeapon, NearestOf(v, BotView::Seen::kPickup, true), p);
    for (uint32_t bit : kActionBits)
    {
        *p++ = (m.lastButtons & bit) != 0; // what it did last
    }
    *p++ = 1;
    // Added later, so at the end (rl_bots.py widens a trained network with zero weights for them): the nearest
    // players' cars
    std::vector<std::pair<float, bool>> drivers;
    for (const BotView::Seen& s : v.seen)
    {
        if (s.kind == BotView::Seen::kPlayer && s.health > 0)
        {
            drivers.push_back(std::make_pair(hypotf(s.x - v.x, s.y - v.y), s.inCar));
        }
    }
    std::sort(drivers.begin(), drivers.end());
    for (int i = 0; i < kPlayers; i++)
    {
        *p++ = i < (int)drivers.size() && drivers[i].second;
    }
    if (p - out != kBotFeatures)
    {
        abort(); // kBotFeatures is out of date
    }
}

uint32_t net::BotButtons(const int* choice)
{
    static const uint32_t kTurn[3] = {kBtnLeft, 0, kBtnRight}, kMove[3] = {kBtnForward, 0, kBtnBack};
    static const uint32_t kPress[4] = {kBtnAttack, kBtnEnterExit, kBtnJump, kBtnNextWeapon};
    uint32_t b = kTurn[choice[0]] | kMove[choice[1]];
    for (int g = 2; g < kBotGroups; g++)
    {
        b |= choice[g] ? kPress[g - 2] : 0;
    }
    return b;
}

std::unique_ptr<net::BotBrain> net::CreateNeuralBrain(const std::string& path)
{
    std::unique_ptr<NeuralBot> bot(new NeuralBot());
    return bot->Load(path.c_str()) ? std::move(bot) : nullptr;
}
