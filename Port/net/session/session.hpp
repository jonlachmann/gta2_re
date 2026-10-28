// Port/net (docs/netcode.md, "How it works"): what the server (server.hpp) and the client (client.hpp) share. Both run
// inside the game's network frame (engine::SetFrameHook): after a tick, before drawing.
#pragma once
#include "../core/snapshot.hpp"
#include <string>

namespace net
{
const int kRing = 64; // ticks of snapshots, commands and baselines kept (by tick % kRing)

// " p:f/d(h)@x,y": per player, frags/deaths (health) and where
std::string Scoreboard(const Snapshot& snap);
} // namespace net
