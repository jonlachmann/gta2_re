#pragma once
// Port/net (GTA2_NET_JERKLOG): how jerky what is shown moves. Per entity, frame to frame, the change of its step
// (|v - v_prev|, in blocks per tick per tick); counted per kind (players, cars, peds) over a size. The server measures
// the same on its own state: what it has is the game's own (collisions, turns), the client's excess is the netcode's.
#include "../engine/engine.hpp"
#include <math.h>
#include <stdio.h>
#include <unordered_map>
#include <vector>

namespace net
{
class JerkMeter
{
  public:
    void Feed(uint32_t frame, const std::vector<engine::Pose>& poses, uint32_t localId = 0) // localId: the client's own player
    {
        std::unordered_map<uint32_t, Track> next;
        for (const engine::Pose& p : poses)
        {
            Track t = {p.x / 16384.0f, p.y / 16384.0f, 0, 0, frame, false};
            auto it = mTracks.find(p.id);
            if (it != mTracks.end() && it->second.frame + 1 == frame)
            {
                const Track& o = it->second;
                t.vx = t.x - o.x;
                t.vy = t.y - o.y;
                t.moving = true;
                const float step = sqrtf(t.vx * t.vx + t.vy * t.vy);
                Kind& k = mKinds[p.id == localId ? 3 : p.id < 0x01000000u ? 0 : p.id < 0x02000000u ? 1 : 2];
                if (step > 2)
                {
                    k.jumps++; // a respawn, a car entered: not motion
                    t.moving = false;
                }
                else if (o.moving)
                {
                    const float ax = t.vx - o.vx, ay = t.vy - o.vy, a = sqrtf(ax * ax + ay * ay);
                    k.frames++;
                    k.over[0] += a > 0.05f;
                    k.over[1] += a > 0.15f;
                    k.over[2] += a > 0.4f;
                    k.sum += a;
                }
            }
            next[p.id] = t;
        }
        mTracks.swap(next);
    }
    void Report(const char* who, uint32_t frame)
    {
        static const char* const kName[4] = {"players", "cars", "peds", "own player"};
        for (int i = 0; i < 4; i++)
        {
            const Kind& k = mKinds[i];
            printf("net: %s frame %u, jerk of %s: %u entity frames, %.4f blocks/tick^2 on average, over 0.05: %.2f%%, over 0.15: %.2f%%, "
                   "over 0.4: %.2f%%, %u jumps\n",
                   who, frame, kName[i], k.frames, k.frames ? k.sum / k.frames : 0.0f, k.frames ? 100.0f * k.over[0] / k.frames : 0.0f,
                   k.frames ? 100.0f * k.over[1] / k.frames : 0.0f, k.frames ? 100.0f * k.over[2] / k.frames : 0.0f, k.jumps);
            mKinds[i] = Kind();
        }
    }

  private:
    struct Track
    {
        float x, y, vx, vy;
        uint32_t frame;
        bool moving;
    };
    struct Kind
    {
        uint32_t frames = 0, jumps = 0, over[3] = {0, 0, 0};
        float sum = 0;
    };
    std::unordered_map<uint32_t, Track> mTracks;
    Kind mKinds[4];
};
} // namespace net
