// Port/net dev (docs/netcode.md, "Checks"): the client's checks and test logs, each off unless its GTA2_NET_* switch is
// set: the comparison check (the prediction against each snapshot), the divergence check (on our own from a tick), the
// players' car check, the corrections, animation, input, self and sound logs, the jerk statistics, the server marker and
// test commands. The client calls it at fixed places in its frame and its reconcile.
#pragma once
#include "../core/snapshot.hpp"
#include "../engine/engine.hpp"
#include "jerk.hpp"
#include <functional>
#include <stdint.h>
#include <string>
#include <vector>

namespace net
{
class ClientProbe
{
  public:
    void Start(); // welcomed
    void Frame(uint32_t frame, const std::vector<uint32_t>& remote); // the frame's start
    void AfterTick(); // the frame's tick ran
    void Input(uint32_t frame, uint32_t local); // the input chosen for it
    void Say(uint32_t frame, bool connected, const std::function<void(const std::string&)>& send); // GTA2_NET_SAY
    void Marker(const EntityState& self); // the local player in the newest snapshot
    // Reconcile: the game restored to the snapshot's tick; the snapshot applied (true: the divergence check starts, the
    // game runs on its own from here); a tick resimulated
    void AfterRestore(const Snapshot& snap);
    bool AfterApply(const Snapshot& snap, uint32_t ourTick);
    void AfterResim();
    // The divergence check, once on our own: each snapshot against our game at its tick (<saved>), then back to <now>;
    // the program ends after its ticks
    bool Alone() const
    {
        return mAloneFrom != 0;
    }
    void CompareAlone(const Snapshot& snap, engine::SavedWorld* saved, engine::SavedWorld& now);
    // A correction blended in (in raw units): logged when large (GTA2_NET_CORRLOG)
    void Correction(uint32_t frame, const engine::Pose& n, int32_t dx, int32_t dy, int32_t dr, const Snapshot& latest);
    void Jerk(uint32_t frame, int slot, bool synced); // what is shown around the local player, this frame
    void PrintStats(uint32_t frame); // with the client's periodic log lines
    void FrameEnd(); // the frame's last: the game as it is drawn

  private:
    JerkMeter mJerk;
    uint32_t mAloneFrom = 0; // the server tick from which this game runs on its own (0: not yet)
    uint32_t mLastInput = ~0u;
};
} // namespace net
