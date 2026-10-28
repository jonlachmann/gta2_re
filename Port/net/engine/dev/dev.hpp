// Port/net engine dev (docs/netcode.md, "Checks"): the engine's checks, logs and test cheats, for Port/net/dev and the
// session's logs. Each is off unless its GTA2_NET_* switch is set (Debug()), or called by a test hook only.
#pragma once
#include "../engine.hpp"

class Camera;
class Car;

namespace net
{
namespace engine
{
// The debug switches (GTA2_NET_*), read once (checks.cpp)
struct DebugFlags
{
    bool carDiff, selfLog, animLog, soundLog, checkGrid, pickupLog;
};
const DebugFlags& Debug();
bool DebugGridOk(const char* where, Car* car); // GTA2_NET_CHECKGRID: the collision grid has no freed sprites
void DebugCarDiff(Car* car, const EntityState& e, uint32_t tick); // GTA2_NET_CARDIFF: the physics members that differ (cars.cpp)
void DebugDrawMarker(Camera* cam); // GTA2_NET_MARKER's square, over the frame (overlay.cpp)

// Test cheats (test_hooks.cpp)
// The game's own death for a player, credited to another (-1: nobody); a server's frags follow from it
void DebugKill(int victim, int killer);
void DebugArm(int player, int weapon, int ammo); // Player::AddWeaponWithAmmo_564960
void DebugWanted(int player, int points); // at least that many wanted points (Ped::field_20A_wanted_points)
void DebugPowerUp(int player, int powerUp); // Player::CollectPowerUp_564D60 (power_up_indices, e.g. 12 instant gang)
void DebugCar(int player, int model); // a car beside the player (GTA2_NET_TESTCAR)

// Logs and checks (checks.cpp; the comparison and divergence checks and the dumps: compare.cpp)
void GangLog(bool server); // GTA2_NET_GANGLOG
// GTA2_NET_CARDIFF: where this game's prediction of the local player's car differs from the snapshot's (call after
// restoring the snapshot's tick, before applying it)
void DebugOwnCarDiff(const Snapshot& snap);
// GTA2_NET_COMPARE (the comparison check): the game restored to the snapshot's tick, its fields against the snapshot's,
// tallied; printed (the fields that differ most) and cleared by PrintPredictionStats
void ComparePrediction(const Snapshot& snap);
void PrintPredictionStats(uint32_t frame);
// The divergence check: the server's state at a tick against this game's after age ticks on its own (0: just applied)
void CompareDivergence(const Snapshot& snap, uint32_t age);
void PrintDivergence();
// GTA2_NET_DUMPPEDS=<file>[:<from>:<to>]: every ped's Ped, Char and Sprite as bytes, by the server's ids, at a tick
// (Scripts/port/net/ped_diff.py compares a server's dump with a client's)
void DumpPeds(uint32_t tick, bool server);
// GTA2_NET_RANDLOG=<file>:<from>:<to>: each rand() draw's caller (an offset in the program) at those ticks (the game's
// cycle), from when StartRandLog is called; Scripts/port/net/rand_diff.py compares two
void StartRandLog();
void DebugSoundChannels(uint32_t frame); // GTA2_NET_SOUNDLOG: the sound's playing channels, printed when they change
void DebugAnimLog(uint32_t frame); // GTA2_NET_ANIMLOG: the other players' inputs, states and walking animation, every third frame
void DebugSelfLog(uint32_t frame); // GTA2_NET_SELFLOG: the local player around a death (position, states, the death fields)
std::string DebugPlayerCar(int player, const Snapshot& snap); // for the logs: its car states here and in the snapshot
bool DebugCheckPlayerCars(const char* where); // every in-game player's car is a live car; prints the first time not
void DebugOnCrash(); // from the crash handler (Port/main.cpp): the local player's ped, car and camera
void DebugSetFrame(uint32_t frame); // the client's frame, for the debug checks' messages
// GTA2_NET_MARKER: a square drawn where the newest snapshot has the local player (raw Fix16), over the frame
void SetServerMarker(int32_t x, int32_t y, int32_t z);
} // namespace engine
} // namespace net
