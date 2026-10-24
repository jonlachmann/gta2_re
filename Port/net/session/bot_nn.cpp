// Port/net: the neural network bot ("nn:<file>", bot.hpp). A small feed-forward network maps what the bot sees,
// relative to where it faces, to its buttons. Its weights come from Scripts/port/train_bots.py, which evolves them in
// matches against each other (docs/netcode.md, "Bots"). The shape here and there must agree: the file's first line
// says it.
#include "bot.hpp"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
const float kPi = 3.14159265f;
const int kPlayers = 3, kCars = 2, kPeds = 1, kPickups = 2; // the nearest ones it sees
const int kRays = 8; // how far it can walk, in 8 directions
const int kOutputs = 10; // turn left/none/right, forward/none/back, attack, enter/exit, jump, next weapon
const int kInputs = 3 + 4 * kPlayers + 3 * (kCars + kPeds + kPickups) + kRays + kOutputs + 1;
const int kHidden = 32;
const float kRange = 16; // blocks: farther is "far"

float Clamp(float v)
{
    return v < -1 ? -1 : v > 1 ? 1 : v;
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
        int in = 0, hidden = 0, out = 0;
        bool ok = fscanf(f, "nn %d %d %d", &in, &hidden, &out) == 3 && in == kInputs && hidden == kHidden && out == kOutputs;
        mW1.resize(kInputs * kHidden);
        mW2.resize((kHidden + 1) * kOutputs);
        for (float& w : mW1)
        {
            ok = ok && fscanf(f, "%f", &w) == 1;
        }
        for (float& w : mW2)
        {
            ok = ok && fscanf(f, "%f", &w) == 1;
        }
        fclose(f);
        if (!ok)
        {
            fprintf(stderr, "net: %s is no network of %d inputs, %d hidden, %d outputs\n", path, kInputs, kHidden, kOutputs);
        }
        return ok;
    }

    uint32_t Think(const net::BotView& v) override
    {
        if (!v.hasBody || v.health <= 0)
        {
            mLastX = -1;
            return 0;
        }
        float in[kInputs];
        Perceive(v, in);
        float hidden[kHidden + 1];
        for (int h = 0; h < kHidden; h++)
        {
            float sum = 0;
            for (int i = 0; i < kInputs; i++)
            {
                sum += mW1[h * kInputs + i] * in[i];
            }
            hidden[h] = tanhf(sum);
        }
        hidden[kHidden] = 1;
        for (int o = 0; o < kOutputs; o++)
        {
            float sum = 0;
            for (int h = 0; h <= kHidden; h++)
            {
                sum += mW2[o * (kHidden + 1) + h] * hidden[h];
            }
            mOut[o] = sum;
        }
        static const uint32_t kTurn[3] = {net::kBtnLeft, 0, net::kBtnRight};
        static const uint32_t kMove[3] = {net::kBtnForward, 0, net::kBtnBack};
        static const uint32_t kBits[4] = {net::kBtnAttack, net::kBtnEnterExit, net::kBtnJump, net::kBtnNextWeapon};
        const int turn = (int)(std::max_element(mOut, mOut + 3) - mOut), move = (int)(std::max_element(mOut + 3, mOut + 6) - mOut - 3);
        mButtons = kTurn[turn] | kMove[move];
        for (int b = 0; b < 4; b++)
        {
            mButtons |= mOut[6 + b] > 0 ? kBits[b] : 0;
        }
        return mButtons;
    }

    std::string Describe() const override
    {
        char line[64];
        snprintf(line, sizeof(line), "buttons 0x%x", mButtons);
        return line;
    }

  private:
    // Into the bot's own frame: ahead and to its left, in kRange
    void Relative(const net::BotView& v, float x, float y, float* out) const
    {
        // Facing rot walks along (sin, cos) (bot.hpp); its left is towards larger angles
        const float a = v.rot * kPi / 720.0f, dx = x - v.x, dy = y - v.y;
        const float ahead = dx * sinf(a) + dy * cosf(a), left = dx * cosf(a) - dy * sinf(a);
        out[0] = Clamp(ahead / kRange);
        out[1] = Clamp(left / kRange);
        out[2] = Clamp(hypotf(dx, dy) / kRange);
    }

    // The <n> nearest of a kind, 3 values each (and the player's health): none is far ahead
    float* Nearest(const net::BotView& v, net::BotView::Seen::Kind kind, int n, float* out) const
    {
        std::vector<std::pair<float, const net::BotView::Seen*>> sorted;
        for (const net::BotView::Seen& s : v.seen)
        {
            if (s.kind == kind && (kind != net::BotView::Seen::kPlayer || s.health > 0))
            {
                sorted.push_back(std::make_pair(hypotf(s.x - v.x, s.y - v.y), &s));
            }
        }
        std::sort(sorted.begin(), sorted.end(), [](const std::pair<float, const net::BotView::Seen*>& a, const std::pair<float, const net::BotView::Seen*>& b) {
            return a.first < b.first;
        });
        for (int i = 0; i < n; i++)
        {
            if (i < (int)sorted.size())
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
                *out++ = i < (int)sorted.size() ? sorted[i].second->health / 100.0f : 0;
            }
        }
        return out;
    }

    // How far it can walk from its cell in <dir> (0..7, a turn of 45 degrees each from ahead), 0..1 of 6 cells
    float Ray(const net::BotView& v, int dir) const
    {
        if (!v.map)
        {
            return 1;
        }
        const int N = net::engine::NavMap::kSize;
        const float a = v.rot * kPi / 720.0f + dir * kPi / 4, sx = sinf(a), sy = cosf(a);
        int cx = (int)v.x, cy = (int)v.y;
        for (float t = 0.5f; t <= 6; t += 0.5f)
        {
            const int nx = (int)(v.x + sx * t), ny = (int)(v.y + sy * t);
            if (nx < 0 || ny < 0 || nx >= N || ny >= N)
            {
                return t / 6;
            }
            // A step to the next cell: along x, then y (a diagonal one through either)
            const auto canGo = [&](int x, int y, int tx, int ty) {
                const uint8_t w = v.map->ways[y * N + x];
                return tx > x ? (w & net::engine::NavMap::kEast) : tx < x ? (w & net::engine::NavMap::kWest)
                       : ty > y ? (w & net::engine::NavMap::kSouth) : ty < y ? (w & net::engine::NavMap::kNorth) : true;
            };
            if ((nx != cx || ny != cy) &&
                !((canGo(cx, cy, nx, cy) && canGo(nx, cy, nx, ny)) || (canGo(cx, cy, cx, ny) && canGo(cx, ny, nx, ny))))
            {
                return t / 6;
            }
            cx = nx, cy = ny;
        }
        return 1;
    }

    void Perceive(const net::BotView& v, float* in)
    {
        float* p = in;
        *p++ = v.health / 100.0f;
        *p++ = v.car != 0;
        *p++ = mLastX < 0 ? 0 : Clamp(hypotf(v.x - mLastX, v.y - mLastY) * 4); // its speed, blocks per tick
        mLastX = v.x, mLastY = v.y;
        p = Nearest(v, net::BotView::Seen::kPlayer, kPlayers, p);
        p = Nearest(v, net::BotView::Seen::kCar, kCars, p);
        p = Nearest(v, net::BotView::Seen::kPed, kPeds, p);
        p = Nearest(v, net::BotView::Seen::kPickup, kPickups, p);
        for (int r = 0; r < kRays; r++)
        {
            *p++ = Ray(v, r);
        }
        for (int o = 0; o < kOutputs; o++)
        {
            *p++ = Clamp(mOut[o]); // what it did last: a memory of a tick
        }
        *p++ = 1;
    }

    std::vector<float> mW1, mW2; // rows: hidden (inputs, the last the bias), outputs (hidden and a bias)
    float mOut[kOutputs] = {};
    float mLastX = -1, mLastY = 0;
    uint32_t mButtons = 0;
};
} // namespace

std::unique_ptr<net::BotBrain> net::CreateNeuralBrain(const std::string& path)
{
    std::unique_ptr<NeuralBot> bot(new NeuralBot());
    return bot->Load(path.c_str()) ? std::move(bot) : nullptr;
}
