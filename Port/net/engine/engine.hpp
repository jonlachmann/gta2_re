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
void SetTickMs(int ms); // the length of a network tick (the game's own setting offers 50, 40 or 33 ms)

// The net schema (access.cpp): entities as plain numbers
const Schema& NetSchema();
enum EntityKind : uint8_t
{
    kEntityPlayer = 0,
    kEntityCar = 1,
    kEntityPed = 2,
};
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

// Where this game has its mapped cars and peds now: NetId and x, y in blocks
struct ReplicaPos
{
    uint32_t id;
    float x, y;
};
void ReplicaPositions(std::vector<ReplicaPos>& out);

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
void DebugOnCrash(); // from the crash handler (Port/main.cpp): the local player's ped, car and camera // every in-game player's car is a live car; prints the first time not
void DebugArm(int player, int weapon, int ammo); // Player::AddWeaponWithAmmo_564960, for tests // in this game: dead and waiting to respawn (Player::field_28_wasted_or_busted)

// What a client doesn't simulate (client_mode.cpp): spawning and despawning, scripts, police; called every frame
void ClientModeFrame();
} // namespace engine
} // namespace net
