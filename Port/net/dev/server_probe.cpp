#include "server_probe.hpp"
#include "../engine/dev/dev.hpp"
#include "../session/env.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
// <tick>:<a>:<b>[,...] into events of <kind>; <minFields> of the three needed
void Parse(const char* list, int kind, int minFields, int defaultB, std::vector<int>& out)
{
    for (const char* k = list; k && *k; k = strchr(k, ','), k = k ? k + 1 : NULL)
    {
        unsigned tick = 0;
        int a = 0, b = defaultB;
        if (sscanf(k, "%u:%d:%d", &tick, &a, &b) >= minFields)
        {
            out.insert(out.end(), {kind, (int)tick, a, b});
        }
    }
}
} // namespace

void net::ServerProbe::Start()
{
    std::vector<int> v; // kind, tick, a, b per event
    Parse(Env().testKill, TestEvent::kKill, 2, -1, v); // <tick>:<victim>:<killer>, the game's own death
    Parse(Env().testPowerUp, TestEvent::kPowerUp, 3, 0, v); // <tick>:<player>:<power-up> (12: an instant gang)
    Parse(Env().testCar, TestEvent::kCar, 3, 0, v); // <tick>:<player>:<model> beside the player (11: a bus)
    for (size_t i = 0; i < v.size(); i += 4)
    {
        mEvents.push_back(TestEvent{(TestEvent::Kind)v[i], (uint32_t)v[i + 1], v[i + 2], v[i + 3]});
    }
    if (const char* wanted = Env().testWanted) // <points>: every player at least that wanted (600: a star)
    {
        mWanted = atoi(wanted);
    }
    if (const char* arm = Env().testArm) // <weapon>:<ammo>: every player kept armed (weapon_type, 0 pistol, 1 smg, ...)
    {
        mArmWeapon = 0;
        sscanf(arm, "%d:%d", &mArmWeapon, &mArmAmmo);
    }
    mJerk.PrintJumps(Env().jerkLog >= 2);
}

void net::ServerProbe::Tests(uint32_t tick, int players)
{
    // In the order of the switches: the kills, then the power-ups, then the cars
    for (const TestEvent& e : mEvents)
    {
        if (e.tick != tick)
        {
            continue;
        }
        switch (e.kind)
        {
            case TestEvent::kKill:
                printf("net: test kill of player %d by %d at tick %u\n", e.player, e.arg, tick);
                engine::DebugKill(e.player, e.arg);
                break;
            case TestEvent::kPowerUp:
                printf("net: test power-up %d for player %d at tick %u\n", e.arg, e.player, tick);
                engine::DebugPowerUp(e.player, e.arg);
                break;
            case TestEvent::kCar:
                printf("net: test car %d for player %d at tick %u\n", e.arg, e.player, tick);
                engine::DebugCar(e.player, e.arg);
                break;
        }
    }
    for (int i = 0; i < players; i++)
    {
        if (mWanted >= 0)
        {
            engine::DebugWanted(i, mWanted);
        }
        if (mArmWeapon >= 0)
        {
            engine::DebugArm(i, mArmWeapon, mArmAmmo);
        }
    }
}

void net::ServerProbe::Captured(uint32_t tick, const Snapshot& world)
{
    engine::GangLog(true);
    engine::DumpPeds(tick, true);
    engine::StartRandLog();
    if (!Env().jerkLog)
    {
        return;
    }
    std::vector<engine::Pose> poses;
    for (const EntityState& e : world.entities)
    {
        engine::Pose p;
        if (engine::PoseOf(e, &p))
        {
            poses.push_back(p);
        }
    }
    mJerk.Feed(tick, poses);
    if (tick % 900 == 0)
    {
        mJerk.Report("server", tick);
    }
}
