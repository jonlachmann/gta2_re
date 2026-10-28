// Port/net dev (docs/netcode.md, "Checks"): the server's test hooks and logs, each off unless its GTA2_NET_* switch is
// set: test kills, power-ups and cars at given ticks, every player kept armed or wanted; the gang, ped and rand() logs
// and the jerk statistics of the server's own state. The server calls it at two places in its frame.
#pragma once
#include "../core/snapshot.hpp"
#include "jerk.hpp"
#include <stdint.h>
#include <vector>

namespace net
{
class ServerProbe
{
  public:
    void Start(); // reads the switches
    void Tests(uint32_t tick, int players); // before the world is captured: the test hooks for this tick
    void Captured(uint32_t tick, const Snapshot& world); // the world as captured: the logs

  private:
    struct TestEvent // GTA2_NET_TESTKILL, _TESTPOWERUP, _TESTCAR
    {
        enum Kind
        {
            kKill,
            kPowerUp,
            kCar
        } kind;
        uint32_t tick;
        int player, arg; // the victim and its killer (-1: nobody); the player and the power-up or car model
    };
    std::vector<TestEvent> mEvents;
    int mWanted = -1; // GTA2_NET_TESTWANTED's points
    int mArmWeapon = -1, mArmAmmo = 50; // GTA2_NET_TESTARM
    JerkMeter mJerk;
};
} // namespace net
