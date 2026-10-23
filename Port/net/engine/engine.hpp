// Port/net (docs/netcode.md, "Code layout"): what the session sees of the game. Plain types only: the decomp's
// types stay inside engine/, so the session and modes don't move when the decomp does.
#pragma once
#include "../core/snapshot.hpp"
#include <stdint.h>
#include <string>
#include <vector>

namespace net
{
namespace engine
{
// A network game for the game to boot (netplay.cpp answers the game's network layer with it)
struct NetworkGame
{
    int players = 1;
    int localPlayer = 0; // the server's is a dummy: nobody's input arrives through the keyboard there
    std::string gmp, sty, scr; // file names in data/
    int gameType = 1; // 1 frags, 2 points, 3 tag
    int fragLimit = 10;
    int timeLimitMin = 10;
    int police = 0;
    int tickMs = 33;
};

// Before WinMain: the game boots this network game (with -N on its command line)
void StartNetworkGame(const NetworkGame& game);
const NetworkGame& CurrentNetworkGame();
bool NetworkGameActive();

// Once per network frame, after the tick and before drawing (NetPlay::SendToAll_521B20)
typedef void (*FrameHook)(void* user);
void SetFrameHook(FrameHook hook, void* user);

bool GameRunning(); // a game with its players exists
int PlayerCount();
int LocalPlayer();
uint32_t LocalInput(); // what this machine's controls give for the next tick
void SetPlayerInput(int player, uint32_t buttons); // for the next tick
bool PlayerHasPed(int player);
void SetTickMs(int ms);
// How far a snapshot's area reaches: the server sets it per client (the farthest entity it kept), a client removes
// what is missing only within it (blocks)
void SetSnapshotReach(Snapshot& snap, float blocks);
float SnapshotReach(const Snapshot& snap);
// A slot's name as the game shows it (over the player, in kill messages): Player::field_83C_player_name, up to 15
void SetPlayerName(int slot, const char* name); // the length of a network tick (the game's own setting offers 50, 40 or 33 ms)

// The net schema (access.cpp): entities as plain numbers
const Schema& NetSchema();
enum EntityKind : uint8_t
{
    kEntityPlayer = 0,
    kEntityCar = 1,
    kEntityPed = 2,
    kEntityWorld = 3, // one, kWorldId: game-wide state (no position), in every client's snapshot
    kEntityObject = 4, // a pickup (pickups.cpp)
};
const uint32_t kWorldId = 0;
// Every player, car and ped
void Capture(Snapshot& out);
// Puts a snapshot's players, cars and peds into this game
void Apply(const Snapshot& snap);
// The scores (frags, points) of a snapshot's players: last, after a resimulation, so the server's count is shown
void ApplyScores(const Snapshot& snap);
// Dead reckoning for the cars and peds a client doesn't drive itself: each moved on from <snap> by its velocity
// between <prev> and <snap>, over <ticks> ticks (players move by their own inputs instead)
void Extrapolate(const Snapshot& snap, const Snapshot& prev, int ticks);
// A player entity's player index and input bits; false for other entities
bool PlayerOf(const EntityState& e, int* pPlayer, uint32_t* pButtons);
int PlayerHealth(const EntityState& e); // a player entity's health
int PlayerFrags(const EntityState& e);
int PlayerDeaths(const EntityState& e);
uint32_t PlayerCarId(const EntityState& e); // the NetId of the car it drives, 0 on foot

// Rollback (rollback.cpp): the whole game world and this client's replicas, saved after a tick and restored to
// resimulate from there
class SavedWorld
{
  public:
    SavedWorld();
    ~SavedWorld();
    SavedWorld(const SavedWorld&) = delete;
    SavedWorld& operator=(const SavedWorld&) = delete;
    uint32_t tick = 0;
    bool valid = false;
    struct Impl;
    Impl* mImpl;
};
void SaveGame(SavedWorld& out, uint32_t tick);
void RestoreGame(const SavedWorld& in);
// One tick without drawing or sound, with every player's buttons (count: players)
void ResimTick(const uint32_t* buttons, int count);
// The position of a player's ped or car, in blocks; false without a ped
bool PlayerPosition(int player, float* pX, float* pY);

// A client's replicas (replica.cpp): how many cars and peds are mapped, and how far (in blocks, the largest) the
// mapped ones are from <snap> right now, before it is applied: the drift of one tick of the client's own sim
void ReplicaStats(const Snapshot& snap, int* pCars, int* pPeds, float* pMaxDrift);
void PoolUse(int* pCars, int* pPeds, int* pChars); // live cars, peds, chars in this game
// Drivers: how many peds the snapshot has in a car, how many of their cars have a driver here, and how many of
// those are the same (mapped) ped
void DriverStats(const Snapshot& snap, int* pSent, int* pDriven, int* pSame);
// Traffic cars: how many the snapshot has with the shared traffic driver, and how many have it here too
// GTA2_NET_CARDIFF: where this game's prediction of the local player's car differs from the snapshot's (call after
// restoring the snapshot's tick, before applying it)
void DebugOwnCarDiff(const Snapshot& snap);
// GTA2_NET_SOUNDLOG: the sound's playing channels, printed when they change
void DebugSoundChannels(uint32_t frame);
// GTA2_NET_ANIMLOG: the other players' inputs, states and walking animation, every third frame
void DebugAnimLog(uint32_t frame);
// GTA2_NET_SELFLOG: the local player around a death (position, states, the death fields), every frame
void DebugSelfLog(uint32_t frame);
// GTA2_NET_MARKER: a square drawn where the newest snapshot has the local player (raw Fix16), over the frame
void SetServerMarker(int32_t x, int32_t y, int32_t z);
void DrawOverlay(); // the game calls it while drawing (Game::Draw_4B92D0, gPortDrawOverlay)
void TrafficDriverStats(const Snapshot& snap, int* pSent, int* pSame);

// Where this game has its mapped cars and peds now: NetId and x, y in blocks
struct ReplicaPos
{
    uint32_t id;
    float x, y;
};
void ReplicaPositions(std::vector<ReplicaPos>& out);

// Where the players on foot, and the cars and peds the snapshots map, are now (raw Fix16, Ang16 0..1439), and a
// move of one to a pose (grid-safe; players in a car or halfway in or out, dead ones and cars a carrier holds
// stay). For blending a correction in (client.cpp)
struct Pose
{
    uint32_t id;
    int32_t x, y, z;
    int32_t rot;
};
void Poses(std::vector<Pose>& out);
void SetPose(const Pose& p);

// The static map as a bot may know it (navmap.cpp): for every cell the height a walker is at, and which of its four
// neighbours it can walk to. Plain data, built once per map, so a bot plans its own way without the game's routes.
struct NavMap
{
    static const int kSize = 256;
    enum Way : uint8_t
    {
        kNorth = 1, // -y
        kSouth = 2, // +y
        kWest = 4, // -x
        kEast = 8, // +x
    };
    uint8_t height[kSize * kSize]; // 255: nobody stands here
    uint8_t ways[kSize * kSize];
};
void BuildNavMap(NavMap& out);
// Where this game has a player now (x, y, z in blocks; rot 0..1439); false without a ped
bool PlayerPose(int player, float* pX, float* pY, float* pZ, int* pRot);

// Player slots in a running network game (slots.cpp): out of the game as the game handles a player who left, in
// again with a new ped at the next restart zone (and an empty score if <freshScore>)
bool SlotInGame(int slot);
void TakeOutOfGame(int slot);
void PutInGame(int slot, bool freshScore);

// Tests: the game's own death for a player, credited to another (-1: nobody); a server's frags follow from it
void DebugKill(int victim, int killer);
bool PlayerWasted(int player);
bool PlayerInCar(int player); // in this game
bool DebugCheckPlayerCars(const char* where);
void DebugOnCrash();
void DebugSetFrame(uint32_t frame); // the client's frame, for the debug checks' messages // from the crash handler (Port/main.cpp): the local player's ped, car and camera // every in-game player's car is a live car; prints the first time not
void DebugArm(int player, int weapon, int ammo); // Player::AddWeaponWithAmmo_564960, for tests // in this game: dead and waiting to respawn (Player::field_28_wasted_or_busted)

// What a client doesn't simulate (client_mode.cpp): spawning and despawning, scripts, police; called every frame
void ClientModeFrame();
} // namespace engine
} // namespace net
