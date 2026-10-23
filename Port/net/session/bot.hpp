// Port/net (docs/netcode.md, "Bots"): the simple first bot. It plays through the same inputs a person's client
// sends: chase the nearest other player, shoot when close and roughly facing it; wander when alone.
#pragma once
#include "../core/snapshot.hpp"
#include <stdint.h>

namespace net
{
class Bot
{
  public:
    // The input bits for the next tick, from the newest snapshot (players: engine::PlayerOf and the schema's first fields)
    uint32_t Think(const Snapshot& snap, int slot, uint32_t frame);

  private:
    uint32_t mSeed = 1;
    uint32_t mWander = 0;
};
} // namespace net
