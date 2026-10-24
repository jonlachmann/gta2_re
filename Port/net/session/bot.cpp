// Port/net: the bot's view (what its client knows) and the simple bot (bot.hpp).
#include "bot.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

// 20 degrees either way is straight on: with less, one tick's turn overshot and the bot pressed left and right in
// turn, every tick, as no person does (a client predicting it from its last input was then wrong every tick)
uint32_t Steer(int err)
{
    return err > 80 ? net::kBtnLeft : err < -80 ? net::kBtnRight : 0;
}

// The nearest of a kind (players: living ones; pickups: weapons only, if asked); NULL if none
const net::BotView::Seen* Nearest(const net::BotView& v, net::BotView::Seen::Kind kind, float* pDist, bool weapons = false)
{
    const net::BotView::Seen* best = NULL;
    *pDist = 1e9f;
    for (const net::BotView::Seen& s : v.seen)
    {
        const float d = hypotf(s.x - v.x, s.y - v.y);
        if (s.kind == kind && d < *pDist && (kind != net::BotView::Seen::kPlayer || s.health > 0) && (!weapons || s.weapon))
        {
            *pDist = d;
            best = &s;
        }
    }
    return best;
}

// On foot, to the nearest car and in: walking at it, then a press now and then (the game walks us to the door), or
// looking for one
uint32_t ApproachCar(const net::BotView& v)
{
    float d = 0;
    const net::BotView::Seen* car = Nearest(v, net::BotView::Seen::kCar, &d);
    if (!car)
    {
        return net::kBtnForward | (v.frame % 90 < 20 ? net::kBtnLeft : 0);
    }
    if (d < 2.5f)
    {
        return v.frame % 20 < 2 ? net::kBtnEnterExit : 0;
    }
    const int err = TurnTo(v.rot, car->x - v.x, car->y - v.y);
    return Steer(err) | (abs(err) < 300 ? net::kBtnForward : 0);
}

// A brain that decides its buttons each frame and remembers them for its log line
class DecidingBot : public net::BotBrain
{
  public:
    uint32_t Think(const net::BotView& v) override
    {
        mButtons = Decide(v);
        return mButtons;
    }

  protected:
    virtual uint32_t Decide(const net::BotView& v) = 0;
    uint32_t mButtons = 0;
};

// Walks to the nearest player along its own route (replanned every second, or when the target moves to another
// cell), turns to face it and shoots when it is close; backs off and turns when stuck. Without a gun and nobody
// within 2 blocks, it fetches the nearest weapon first.
class SimpleBot : public DecidingBot
{
  public:
    std::string Describe() const override
    {
        char line[160];
        snprintf(line, sizeof(line), "at (%.1f, %.1f) rot %d, target %.1f blocks away, route %zu cells, buttons 0x%x%s%s", mX, mY, mRot,
                 mTargetDist, mRoute.path.size(), mButtons, mRoute.none ? " (no route found)" : "", mStuck ? ", stuck" : "");
        return line;
    }

  private:
    uint32_t Decide(const net::BotView& v) override
    {
        if (!v.hasBody || v.health <= 0)
        {
            return 0;
        }
        mX = v.x;
        mY = v.y;
        mRot = v.rot;
        float best = 0;
        const net::BotView::Seen* target = Nearest(v, net::BotView::Seen::kPlayer, &best);
        // Stuck: hardly moved for 1.5 s while trying to: back up and turn for half a second
        if (v.frame % 45 == 0)
        {
            mStuck = mTrying && hypotf(v.x - mLastX, v.y - mLastY) < 0.3f;
            mLastX = v.x;
            mLastY = v.y;
            if (mStuck)
            {
                mUnstickUntil = v.frame + 15;
                mRoute.path.clear();
            }
        }
        if (v.frame < mUnstickUntil)
        {
            return net::kBtnBack | net::kBtnLeft;
        }
        mTrying = false;
        mTargetDist = target ? best : -1;
        float weaponDist = 0;
        const net::BotView::Seen* weapon = v.guns == 0 && (!target || best >= 2.0f) ? Nearest(v, net::BotView::Seen::kPickup, &weaponDist, true) : NULL;
        if (weapon)
        {
            return Along(v, weapon->x, weapon->y);
        }
        if (!target)
        {
            return 0;
        }
        const float dx = target->x - v.x, dy = target->y - v.y;
        if (best < 6.0f) // close: face it and fight
        {
            const int err = TurnTo(v.rot, dx, dy);
            uint32_t b = Steer(err);
            const float reach = v.guns ? 1.2f : 0.8f; // fists only reach that far
            if (best > reach)
            {
                b |= net::kBtnForward;
                mTrying = true;
            }
            else if (best < 0.4f)
            {
                b |= net::kBtnBack; // too close to hit: a step back
            }
            if (abs(err) < 100 && v.frame % 4 < 2) // pressed and released: every press is a punch
            {
                b |= net::kBtnAttack;
            }
            return b;
        }
        return Along(v, target->x, target->y); // far
    }

    // To (tx, ty) along the route
    uint32_t Along(const net::BotView& v, float tx, float ty)
    {
        float wx = 0, wy = 0;
        mRoute.Next(v, tx, ty, &wx, &wy);
        const int err = TurnTo(v.rot, wx - v.x, wy - v.y);
        uint32_t b = Steer(err);
        if (abs(err) < 300) // walk while roughly facing the way
        {
            b |= net::kBtnForward;
            mTrying = true;
        }
        return b;
    }

    float mX = 0, mY = 0, mTargetDist = -1;
    int mRot = 0;
    net::BotRoute mRoute;
    float mLastX = 0, mLastY = 0;
    bool mTrying = false, mStuck = false;
    uint32_t mUnstickUntil = 0;
};
// Gets into the nearest car, drives about 10 s with a turn now and then, gets out, and again
class CarHopBot : public DecidingBot
{
  public:
    std::string Describe() const override
    {
        char line[128];
        snprintf(line, sizeof(line), "%s, %d cars entered, %d left, buttons 0x%x", mInCar ? "driving" : "on foot", mEntered, mLeft, mButtons);
        return line;
    }

  private:
    uint32_t Decide(const net::BotView& v) override
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
        return ApproachCar(v);
    }

    bool mInCar = false;
    uint32_t mSince = 0;
    int mEntered = 0, mLeft = 0;
};
// Gets into the nearest car and drives at the nearest other player, to hit it; backs out with the wheel turned the
// other way when it hasn't moved for a second. Gets out only when it has to (a wreck, a death), then the next car.
class RamBot : public DecidingBot
{
  public:
    std::string Describe() const override
    {
        char line[128];
        snprintf(line, sizeof(line), "%s, %d cars entered, %d times backing out, buttons 0x%x", mInCar ? "driving" : "on foot", mEntered,
                 mReversals, mButtons);
        return line;
    }

  private:
    uint32_t Decide(const net::BotView& v) override
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
        }
        if (!inCar)
        {
            return ApproachCar(v);
        }
        // Stuck: hardly moved over the last second
        if (v.frame % 30 == 0)
        {
            const bool still = hypotf(v.x - mLastX, v.y - mLastY) < 0.5f;
            mLastX = v.x;
            mLastY = v.y;
            if (still && v.frame >= mBackUntil)
            {
                mBackUntil = v.frame + 30;
                mReversals++;
            }
        }
        float d = 0;
        const net::BotView::Seen* target = Nearest(v, net::BotView::Seen::kPlayer, &d);
        const int err = target ? TurnTo(v.rot, target->x - v.x, target->y - v.y) : (v.frame / 60 % 2 ? 200 : -200); // none in sight: wander
        if (v.frame < mBackUntil)
        {
            return net::kBtnBack | (err > 0 ? net::kBtnRight : net::kBtnLeft); // backing, the wheel the other way
        }
        return net::kBtnForward | Steer(err);
    }

    bool mInCar = false;
    int mEntered = 0, mReversals = 0;
    float mLastX = 0, mLastY = 0;
    uint32_t mBackUntil = 0;
};
// The fighter on foot and the rammer in a car: takes a car when one is near and nobody is close, drives at people,
// and fights on foot otherwise (a teacher for the trained bots: one brain, so what it shows is consistent)
class HunterBot : public DecidingBot
{
  public:
    std::string Describe() const override
    {
        return (mDriving ? "driving: " + mCar.Describe() : "on foot: " + mFoot.Describe());
    }

  private:
    uint32_t Decide(const net::BotView& v) override
    {
        float enemy = 0, car = 0;
        Nearest(v, net::BotView::Seen::kPlayer, &enemy);
        Nearest(v, net::BotView::Seen::kCar, &car);
        mDriving = v.car != 0 || (car < 10 && enemy > 8);
        // Both see every tick, so their timers and stuck checks stay right whichever decides
        const uint32_t foot = mFoot.Think(v), drive = mCar.Think(v);
        return mDriving ? drive : foot;
    }

    SimpleBot mFoot;
    RamBot mCar;
    bool mDriving = false;
};
// The whole round: a weapon first, then a car, then along its route to the nearest player, and there whatever suits:
// runs it over when coming at it fast and head on, else gets out and shoots. On foot with a gun it closes in and
// fires while facing it. It doesn't punch.
class SoldierBot : public DecidingBot
{
  public:
    std::string Describe() const override
    {
        char line[160];
        snprintf(line, sizeof(line), "%s, target %.1f blocks away, speed %.2f, %d cars entered, %d left, buttons 0x%x", mInCar ? "driving" : "on foot",
                 mTargetDist, mSpeed, mEntered, mLeft, mButtons);
        return line;
    }

  private:
    uint32_t Decide(const net::BotView& v) override
    {
        if (!v.hasBody || v.health <= 0)
        {
            mBackUntil = 0;
            return 0;
        }
        if ((v.car != 0) != mInCar)
        {
            mInCar = v.car != 0;
            mEntered += mInCar;
            mLeft += !mInCar;
        }
        // Its speed in blocks per tick, smoothed over a few decisions (called every tick or every few)
        const uint32_t ticks = v.frame > mLastFrame ? v.frame - mLastFrame : 1;
        mSpeed = 0.7f * mSpeed + 0.3f * hypotf(v.x - mLastX, v.y - mLastY) / ticks;
        mLastX = v.x, mLastY = v.y, mLastFrame = v.frame;
        float d = 0;
        const net::BotView::Seen* target = Nearest(v, net::BotView::Seen::kPlayer, &d);
        mTargetDist = target ? d : -1;
        if (mInCar)
        {
            return Drive(v, target, d);
        }
        mBackUntil = 0;
        // Stuck on foot (a ped or a wall in the way): hardly moved for a second while walking: back off and turn
        if (v.frame >= mFootCheck)
        {
            mFootStuck = (mButtons & net::kBtnForward) && hypotf(v.x - mFootX, v.y - mFootY) < 0.3f;
            mFootX = v.x, mFootY = v.y, mFootCheck = v.frame + 30;
            mUnstickUntil = mFootStuck ? v.frame + 15 : mUnstickUntil;
        }
        if (v.frame < mUnstickUntil)
        {
            return net::kBtnBack | net::kBtnLeft;
        }
        // Unarmed: to a weapon whatever happens (fistfights between players get stuck, with no harm done)
        float far = 0;
        const net::BotView::Seen* weapon = v.guns ? NULL : Nearest(v, net::BotView::Seen::kPickup, &far, true);
        if (weapon)
        {
            return Walk(v, weapon->x, weapon->y);
        }
        if (target && d < 10 && v.guns)
        {
            return Fight(v, *target, d);
        }
        const net::BotView::Seen* car = Nearest(v, net::BotView::Seen::kCar, &far);
        if (car && far < 15)
        {
            return far < 2.5f ? Press(v, net::kBtnEnterExit) : Walk(v, car->x, car->y); // the game walks it to the door
        }
        return target ? Walk(v, target->x, target->y) : 0;
    }

    // A press now and then, not held: once every 4 decisions of 3 ticks
    static uint32_t Press(const net::BotView& v, uint32_t button)
    {
        return (v.frame / net::kBotDecisionTicks) % 4 == 0 ? button : 0;
    }

    uint32_t Walk(const net::BotView& v, float x, float y)
    {
        float wx = 0, wy = 0;
        mRoute.Next(v, x, y, &wx, &wy);
        const int err = TurnTo(v.rot, wx - v.x, wy - v.y);
        return Steer(err) | (abs(err) < 300 ? net::kBtnForward : 0);
    }

    // Up close and firing: a gun fires straight ahead, and from more than a couple of blocks most shots miss
    uint32_t Fight(const net::BotView& v, const net::BotView::Seen& target, float d)
    {
        const int err = TurnTo(v.rot, target.x - v.x, target.y - v.y);
        uint32_t b = Steer(err);
        if (!b && abs(err) > 20 && (v.frame / net::kBotDecisionTicks) % 2)
        {
            b = err > 0 ? net::kBtnLeft : net::kBtnRight; // nearly there: short turns, which don't overshoot
        }
        // Rockets, molotovs and grenades (Source/enums.hpp, weapon_type) blow it up too up close: those from afar
        const bool blast = v.weapon == 2 || v.weapon == 4 || v.weapon == 5;
        b |= d > (blast ? 7 : 1.5f) ? net::kBtnForward : d < (blast ? 4 : 0.8f) ? net::kBtnBack : 0;
        if (v.weapon < 0)
        {
            return b | Press(v, net::kBtnNextWeapon); // fists in hand: a gun out
        }
        return b | (abs(err) <= 80 ? net::kBtnAttack : 0); // held: guns fire while it is
    }

    uint32_t Drive(const net::BotView& v, const net::BotView::Seen* target, float d)
    {
        // Stuck: hardly moved over the last second: back out with the wheel the other way; stuck again and again: out
        if (v.frame >= mStuckCheck)
        {
            const bool still = hypotf(v.x - mStuckX, v.y - mStuckY) < 0.5f;
            mStuckX = v.x, mStuckY = v.y, mStuckCheck = v.frame + 30;
            mStuckTimes = still ? mStuckTimes + 1 : 0;
            if (still && v.frame >= mBackUntil)
            {
                mBackUntil = v.frame + 30;
            }
        }
        if (!target)
        {
            return net::kBtnForward | (v.frame / 60 % 2 ? net::kBtnLeft : net::kBtnRight); // nobody in sight: about
        }
        const int direct = TurnTo(v.rot, target->x - v.x, target->y - v.y);
        if (v.frame < mBackUntil)
        {
            return mStuckTimes >= 3 ? Press(v, net::kBtnEnterExit) : net::kBtnBack | (direct > 0 ? net::kBtnRight : net::kBtnLeft);
        }
        // Near it, armed, and not coming at it fast and head on: out, to shoot
        if (v.guns && d < 7 && (mSpeed < 0.12f || abs(direct) > 240))
        {
            return Press(v, net::kBtnEnterExit);
        }
        float wx = target->x, wy = target->y;
        if (d > 10)
        {
            mRoute.Next(v, target->x, target->y, &wx, &wy); // far: along the route; near: straight at it
        }
        return net::kBtnForward | Steer(TurnTo(v.rot, wx - v.x, wy - v.y));
    }

    net::BotRoute mRoute;
    bool mInCar = false;
    int mEntered = 0, mLeft = 0;
    float mTargetDist = -1, mSpeed = 0, mLastX = 0, mLastY = 0, mStuckX = 0, mStuckY = 0;
    uint32_t mLastFrame = 0, mStuckCheck = 0, mBackUntil = 0, mFootCheck = 0, mUnstickUntil = 0;
    float mFootX = 0, mFootY = 0;
    bool mFootStuck = false;
    int mStuckTimes = 0;
};
} // namespace

std::unique_ptr<net::BotBrain> net::CreateBotBrain(const std::string& kind)
{
    if (kind == "carhop")
    {
        return std::unique_ptr<BotBrain>(new CarHopBot());
    }
    if (kind == "soldier")
    {
        return std::unique_ptr<BotBrain>(new SoldierBot());
    }
    if (kind == "hunter")
    {
        return std::unique_ptr<BotBrain>(new HunterBot());
    }
    if (kind == "rammer")
    {
        return std::unique_ptr<BotBrain>(new RamBot());
    }
    if (kind.compare(0, 3, "nn:") == 0)
    {
        return CreateNeuralBrain(kind.substr(3));
    }
    if (kind.empty() || kind == "simple")
    {
        return std::unique_ptr<BotBrain>(new SimpleBot());
    }
    return nullptr;
}

void net::FillBotView(BotView& view, const Snapshot& snap, int self, uint32_t frame, const engine::NavMap* map)
{
    view.frame = frame;
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
        s.x = engine::Blocks(e.f[0]); // every entity type with a place starts with x, y, z, rot
        s.y = engine::Blocks(e.f[1]);
        s.z = engine::Blocks(e.f[2]);
        s.rot = e.f[3];
        s.health = 0;
        s.weapon = engine::PickupModel(e) >= 200 && engine::PickupModel(e) <= 227; // Ped::HandlePickupCollision_45DE80
        uint32_t buttons = 0;
        if (engine::PlayerOf(e, &s.player, &buttons))
        {
            s.kind = BotView::Seen::kPlayer;
            s.health = engine::PlayerHealth(e);
            if (s.player == self)
            {
                view.health = s.health;
                view.car = engine::PlayerCarId(e);
                view.weapon = engine::PlayerWeapon(e);
                view.ammo = engine::PlayerAmmo(e, view.weapon);
                view.guns = 0;
                for (int w = 0; w < 15; w++)
                {
                    view.guns += engine::PlayerAmmo(e, w) > 0;
                }
                continue;
            }
        }
        else
        {
            s.kind = e.type == engine::kEntityCar ? BotView::Seen::kCar : e.type == engine::kEntityObject ? BotView::Seen::kPickup : BotView::Seen::kPed;
        }
        view.seen.push_back(s);
    }
}
