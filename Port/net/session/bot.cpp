// Port/net: the bot's view (what its client knows) and the simple bot (bot.hpp).
#include "bot.hpp"
#include <math.h>
#include <stdio.h>

namespace
{
const float kPi = 3.14159265f;

// The angle error from facing <rot> to facing the direction (dx, dy), in game units (-720..720)
int TurnTo(int rot, float dx, float dy)
{
    int err = (int)(atan2f(dx, dy) * 720.0f / kPi) - rot;
    while (err > 720)
    {
        err -= 1440;
    }
    while (err < -720)
    {
        err += 1440;
    }
    return err;
}

uint32_t Steer(int err)
{
    return err > 30 ? net::kBtnLeft : err < -30 ? net::kBtnRight : 0;
}

// Walks to the nearest player along its own route (replanned every second, or when the target moves to another
// cell), turns to face it and shoots when it is close; backs off and turns when stuck
class SimpleBot : public net::BotBrain
{
  public:
    uint32_t Think(const net::BotView& v) override
    {
        mButtons = Decide(v);
        return mButtons;
    }

    uint32_t Decide(const net::BotView& v)
    {
        if (!v.hasBody || v.health <= 0)
        {
            return 0;
        }
        mX = v.x;
        mY = v.y;
        mRot = v.rot;
        const net::BotView::Seen* target = NULL;
        float best = 1e9f;
        for (size_t i = 0; i < v.seen.size(); i++)
        {
            const net::BotView::Seen& s = v.seen[i];
            if (s.kind == net::BotView::Seen::kPlayer && s.health > 0)
            {
                const float d = hypotf(s.x - v.x, s.y - v.y);
                if (d < best)
                {
                    best = d;
                    target = &s;
                }
            }
        }
        // Stuck: hardly moved for 1.5 s while trying to: back up and turn for half a second
        if (v.frame % 45 == 0)
        {
            mStuck = mTrying && hypotf(v.x - mLastX, v.y - mLastY) < 0.3f;
            mLastX = v.x;
            mLastY = v.y;
            if (mStuck)
            {
                mUnstickUntil = v.frame + 15;
                mPath.clear();
            }
        }
        if (v.frame < mUnstickUntil)
        {
            return net::kBtnBack | net::kBtnLeft;
        }
        mTrying = false;
        mTargetDist = target ? best : -1;
        if (!target)
        {
            return 0;
        }
        const float dx = target->x - v.x, dy = target->y - v.y;
        if (best < 6.0f) // close: face it and fight
        {
            const int err = TurnTo(v.rot, dx, dy);
            uint32_t b = Steer(err);
            if (best > 1.2f)
            {
                b |= net::kBtnForward;
                mTrying = true;
            }
            else if (best < 0.6f)
            {
                b |= net::kBtnBack; // too close to hit: a step back
            }
            if ((err < 0 ? -err : err) < 100 && v.frame % 4 < 2) // pressed and released: every press is a punch
            {
                b |= net::kBtnAttack;
            }
            return b;
        }
        // Far: along the route
        const int N = net::engine::NavMap::kSize;
        const int cx = (int)v.x, cy = (int)v.y, gx = (int)target->x, gy = (int)target->y;
        if (v.map && (mPath.empty() || v.frame >= mReplanAt || gy * N + gx != mGoal))
        {
            mGoal = gy * N + gx;
            mReplanAt = v.frame + 30;
            mNoRoute = !net::FindPath(*v.map, cx, cy, gx, gy, mPath);
        }
        // Drop the waypoints up to the one we are on, if it is among the next few (cutting a corner skips some)
        for (size_t i = 0; i < mPath.size() && i < 6; i++)
        {
            if (mPath[i] == cy * N + cx)
            {
                mPath.erase(mPath.begin(), mPath.begin() + i + 1);
                break;
            }
        }
        float wx = target->x, wy = target->y; // no route: straight at it
        if (!mPath.empty())
        {
            // Aim a few cells ahead where the route is straight: smoother than cell by cell
            const size_t ahead = mPath.size() < 3 ? mPath.size() - 1 : 2;
            wx = mPath[ahead] % N + 0.5f;
            wy = mPath[ahead] / N + 0.5f;
        }
        const int err = TurnTo(v.rot, wx - v.x, wy - v.y);
        uint32_t b = Steer(err);
        if ((err < 0 ? -err : err) < 300) // walk while roughly facing the way
        {
            b |= net::kBtnForward;
            mTrying = true;
        }
        return b;
    }

    std::string Describe() const override
    {
        char line[160];
        snprintf(line, sizeof(line), "at (%.1f, %.1f) rot %d, target %.1f blocks away, route %zu cells, buttons 0x%x%s%s", mX, mY, mRot,
                 mTargetDist, mPath.size(), mButtons, mNoRoute ? " (no route found)" : "", mStuck ? ", stuck" : "");
        return line;
    }

  private:
    float mX = 0, mY = 0, mTargetDist = -1;
    uint32_t mButtons = 0;
    int mRot = 0;
    bool mNoRoute = false;
    std::vector<int> mPath;
    int mGoal = -1;
    uint32_t mReplanAt = 0;
    float mLastX = 0, mLastY = 0;
    bool mTrying = false, mStuck = false;
    uint32_t mUnstickUntil = 0;
};
// Gets into the nearest car, drives about 10 s with a turn now and then, gets out, and again
class CarHopBot : public net::BotBrain
{
  public:
    uint32_t Think(const net::BotView& v) override
    {
        mButtons = Decide(v);
        return mButtons;
    }

    std::string Describe() const override
    {
        char line[128];
        snprintf(line, sizeof(line), "%s, %d cars entered, %d left, buttons 0x%x", mInCar ? "driving" : "on foot", mEntered, mLeft, mButtons);
        return line;
    }

  private:
    uint32_t Decide(const net::BotView& v)
    {
        if (!v.hasBody || v.health <= 0)
        {
            return 0;
        }
        const bool inCar = v.car != 0;
        if (inCar != mInCar)
        {
            mInCar = inCar;
            mEntered += inCar;
            mLeft += !inCar;
            mSince = v.frame;
        }
        const uint32_t press = v.frame % 20 < 2 ? net::kBtnEnterExit : 0; // a press now and then, not held
        if (inCar)
        {
            if (v.frame - mSince > 300)
            {
                return press; // out again
            }
            const int phase = (int)((v.frame - mSince) / 45) % 4;
            return net::kBtnForward | (phase == 1 ? net::kBtnLeft : phase == 3 ? net::kBtnRight : 0);
        }
        if (v.frame - mSince < 60)
        {
            return 0; // a moment on foot after getting out
        }
        const net::BotView::Seen* car = NULL;
        float best = 1e9f;
        for (size_t i = 0; i < v.seen.size(); i++)
        {
            const float d = hypotf(v.seen[i].x - v.x, v.seen[i].y - v.y);
            if (v.seen[i].kind == net::BotView::Seen::kCar && d < best)
            {
                best = d;
                car = &v.seen[i];
            }
        }
        if (!car)
        {
            return net::kBtnForward | (v.frame % 90 < 20 ? net::kBtnLeft : 0); // look for one
        }
        if (best < 2.5f)
        {
            return press; // the game walks us to the door
        }
        const int err = TurnTo(v.rot, car->x - v.x, car->y - v.y);
        return Steer(err) | ((err < 0 ? -err : err) < 300 ? net::kBtnForward : 0);
    }

    bool mInCar = false;
    uint32_t mSince = 0;
    int mEntered = 0, mLeft = 0;
    uint32_t mButtons = 0;
};
// Gets into the nearest car and drives at the nearest other player, to hit it; backs out with the wheel turned the
// other way when it hasn't moved for a second. Gets out only when it has to (a wreck, a death), then the next car.
class RamBot : public net::BotBrain
{
  public:
    uint32_t Think(const net::BotView& v) override
    {
        mButtons = Decide(v);
        return mButtons;
    }

    std::string Describe() const override
    {
        char line[128];
        snprintf(line, sizeof(line), "%s, %d cars entered, %d times backing out, buttons 0x%x", mInCar ? "driving" : "on foot", mEntered,
                 mReversals, mButtons);
        return line;
    }

  private:
    const net::BotView::Seen* Nearest(const net::BotView& v, net::BotView::Seen::Kind kind, float* pDist) const
    {
        const net::BotView::Seen* best = NULL;
        *pDist = 1e9f;
        for (size_t i = 0; i < v.seen.size(); i++)
        {
            const float d = hypotf(v.seen[i].x - v.x, v.seen[i].y - v.y);
            if (v.seen[i].kind == kind && d < *pDist && (kind != net::BotView::Seen::kPlayer || v.seen[i].health > 0))
            {
                *pDist = d;
                best = &v.seen[i];
            }
        }
        return best;
    }

    uint32_t Decide(const net::BotView& v)
    {
        if (!v.hasBody || v.health <= 0)
        {
            return 0;
        }
        const bool inCar = v.car != 0;
        if (inCar != mInCar)
        {
            mInCar = inCar;
            mEntered += inCar;
            mStill = 0;
        }
        float d = 0;
        if (!inCar)
        {
            const net::BotView::Seen* car = Nearest(v, net::BotView::Seen::kCar, &d);
            if (!car)
            {
                return net::kBtnForward | (v.frame % 90 < 20 ? net::kBtnLeft : 0); // look for one
            }
            if (d < 2.5f)
            {
                return v.frame % 20 < 2 ? net::kBtnEnterExit : 0; // a press now and then: the game walks us to the door
            }
            const int err = TurnTo(v.rot, car->x - v.x, car->y - v.y);
            return Steer(err) | ((err < 0 ? -err : err) < 300 ? net::kBtnForward : 0);
        }
        // Stuck: hardly moved over the last second
        if (v.frame % 30 == 0)
        {
            const bool still = hypotf(v.x - mLastX, v.y - mLastY) < 0.5f;
            mLastX = v.x;
            mLastY = v.y;
            if (still && v.frame >= mBackUntil && ++mStill >= 1)
            {
                mBackUntil = v.frame + 30;
                mReversals++;
                mStill = 0;
            }
        }
        const net::BotView::Seen* target = Nearest(v, net::BotView::Seen::kPlayer, &d);
        const int err = target ? TurnTo(v.rot, target->x - v.x, target->y - v.y) : (v.frame / 60 % 2 ? 200 : -200); // none in sight: wander
        if (v.frame < mBackUntil)
        {
            return net::kBtnBack | (err > 0 ? net::kBtnRight : net::kBtnLeft); // backing, the wheel the other way
        }
        return net::kBtnForward | Steer(err);
    }

    bool mInCar = false;
    int mEntered = 0, mReversals = 0, mStill = 0;
    float mLastX = 0, mLastY = 0;
    uint32_t mBackUntil = 0;
    uint32_t mButtons = 0;
};
} // namespace

std::unique_ptr<net::BotBrain> net::CreateBotBrain(const std::string& kind)
{
    if (kind == "carhop")
    {
        return std::unique_ptr<BotBrain>(new CarHopBot());
    }
    if (kind == "rammer")
    {
        return std::unique_ptr<BotBrain>(new RamBot());
    }
    if (kind.empty() || kind == "simple")
    {
        return std::unique_ptr<BotBrain>(new SimpleBot());
    }
    return nullptr;
}

void net::FillBotView(BotView& view, const Snapshot& snap, int self, uint32_t frame, const engine::NavMap* map)
{
    view.tick = snap.tick;
    view.frame = frame;
    view.self = self;
    view.map = map;
    view.seen.clear();
    view.hasBody = engine::PlayerPose(self, &view.x, &view.y, &view.z, &view.rot); // as its client predicts it
    view.health = 0;
    for (size_t i = 0; i < snap.entities.size(); i++)
    {
        const EntityState& e = snap.entities[i];
        if (e.type == engine::kEntityWorld)
        {
            continue; // no position
        }
        BotView::Seen s;
        s.id = e.id;
        s.player = -1;
        s.x = e.f[0] / 16384.0f; // every entity type starts with x, y, z, rot
        s.y = e.f[1] / 16384.0f;
        s.z = e.f[2] / 16384.0f;
        s.rot = e.f[3];
        s.health = 0;
        uint32_t buttons = 0;
        if (engine::PlayerOf(e, &s.player, &buttons))
        {
            s.kind = BotView::Seen::kPlayer;
            s.health = engine::PlayerHealth(e);
            if (s.player == self)
            {
                view.health = s.health;
                view.car = engine::PlayerCarId(e);
                continue;
            }
        }
        else
        {
            s.kind = e.type == engine::kEntityCar ? BotView::Seen::kCar : BotView::Seen::kPed;
        }
        view.seen.push_back(s);
    }
}
