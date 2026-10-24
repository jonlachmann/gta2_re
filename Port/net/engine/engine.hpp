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
// --- The game (access.cpp, netplay.cpp, client_mode.cpp) ---

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
// Once per network frame, after the tick and before drawing (NetPlay::SendToAll_521B20)
typedef void (*FrameHook)(void* user);
void SetFrameHook(FrameHook hook, void* user);
bool GameRunning(); // a game with its players exists
int PlayerCount();
int LocalPlayer();
uint32_t LocalInput(); // what this machine's controls give for the next tick
void SetPlayerInput(int player, uint32_t buttons); // for the next tick
// The length of a network tick (the game's own setting offers 50, 40 or 33 ms)
void SetTickMs(int ms);
void SeedRandom(uint32_t seed); // the game's rand() state (a server's: clients take it from snapshots)
// What a client doesn't simulate: spawning and despawning, scripts, police; called every frame
void ClientModeFrame();
// After every tick, on the server too: what the game's own logic can leave behind that crashes it later (client_mode.cpp)
void GuardPeds();

// --- Snapshots (schema.cpp, access.cpp) ---

// The net schema: entities as plain numbers
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
// NetIds: a player's is its slot + 1; cars, peds and pickups have theirs from these on
const uint32_t kCarIdBase = 0x01000000; // + Car::field_6C_car_id
const uint32_t kPedIdBase = 0x02000000; // + Ped::field_200_id
const uint32_t kObjIdBase = 0x03000000; // + Object::field_14_id
inline bool IsPlayerId(uint32_t id)
{
    return id < kCarIdBase;
}
inline bool IsCarId(uint32_t id)
{
    return id >= kCarIdBase && id < kPedIdBase;
}
// A position field (Fix16 raw: 14 fraction bits) in blocks
inline float Blocks(int32_t raw)
{
    return raw / 16384.0f;
}
// Every player, car, ped and pickup
void Capture(Snapshot& out);
// Puts a snapshot's players, cars, peds and pickups into this game
void Apply(const Snapshot& snap);
// The scores (frags, points) of a snapshot's players: last, after a resimulation, so the server's count is shown
void ApplyScores(const Snapshot& snap);
// How far a snapshot's area reaches: the server sets it per client (the farthest entity it kept), a client removes
// what is missing only within it (blocks)
void SetSnapshotReach(Snapshot& snap, float blocks);
float SnapshotReach(const Snapshot& snap);
// The age field of cars, peds and pickups (-1: none): how many ticks old the state is. The server sends the far
// entities of a busy area less often; a client applies only what is new (0) and predicts the rest on
int AgeField(uint8_t type);
int EntityAge(const EntityState& e);

// --- Players (players.cpp) ---

// A player entity's player index and input bits; false for other entities
bool PlayerOf(const EntityState& e, int* pPlayer, uint32_t* pButtons);
int PlayerHealth(const EntityState& e); // a player entity's health
int PlayerFrags(const EntityState& e);
int PlayerDeaths(const EntityState& e);
uint32_t PlayerCarId(const EntityState& e); // the NetId of the car it drives, 0 on foot
int PlayerWeapon(const EntityState& e); // its weapon in hand (Player::field_788_curr_weapon_idx), -1: none
int PlayerAmmo(const EntityState& e, int weapon); // 0..14 on foot, 15..27 a car's
// A pickup entity's Object::field_18_model (objects:: in Source/enums.hpp: 200..227 weapons, 228.. power-ups)
int PickupModel(const EntityState& e);
// The position of a player's ped or car, in blocks; false without a ped
bool PlayerPosition(int player, float* pX, float* pY);
bool PlayerPose(int player, float* pX, float* pY, float* pZ, int* pRot); // and its height and heading (0..1439)
bool PlayerWasted(int player); // dead and waiting to respawn (Player::field_28_wasted_or_busted)
bool PlayerInCar(int player); // in this game
// How far a player's camera shows (blocks, centre to corner): it zooms out with speed
float PlayerViewRadius(int player);
// A slot's name as the game shows it (over the player, in kill messages): Player::field_83C_player_name, up to 15
void SetPlayerName(int slot, const char* name);
// Player slots in a running network game (slots.cpp): out of the game as the game handles a player who left, in
// again with a new ped at the next restart zone (and an empty score if <freshScore>)
bool SlotInGame(int slot);
void TakeOutOfGame(int slot);
void PutInGame(int slot, bool freshScore);

// --- Rollback (rollback.cpp): the whole game world and this client's replicas, saved after a tick and restored to
// resimulate from there ---

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

// --- A client's view (replica.cpp, access.cpp) ---

// Where this game has its mapped cars and peds now: NetId and x, y in blocks
struct ReplicaPos
{
    uint32_t id;
    float x, y;
};
void ReplicaPositions(std::vector<ReplicaPos>& out);
// Where the players on foot, and the cars and peds the snapshots map, are now (raw Fix16, Ang16 0..1439), and a
// move of one to a pose (grid-safe; players in a car or at a car door, dead ones and cars a carrier holds stay). For
// blending a correction in (client.cpp)
struct Pose
{
    uint32_t id;
    int32_t x, y, z;
    int32_t rot;
};
void Poses(std::vector<Pose>& out);
void SetPose(const Pose& p);
bool PoseOf(const EntityState& e, Pose* out); // a snapshot entity's, as Poses has them (players on foot, cars, peds)

// --- Bots (navmap.cpp): the static map as a bot may know it. For every cell the height a walker is at, and which of
// its four neighbours it can walk to. Plain data, built once per map, so a bot plans its own way without the game's
// routes ---

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
void DumpNavMap(const NavMap& map, const char* path); // as a picture (PGM): grey by height, black blocked

// --- Tests, statistics and debugging (debug.cpp, players.cpp) ---

// The game's own death for a player, credited to another (-1: nobody); a server's frags follow from it
void DebugKill(int victim, int killer);
void DebugArm(int player, int weapon, int ammo); // Player::AddWeaponWithAmmo_564960
void DebugPowerUp(int player, int powerUp); // Player::CollectPowerUp_564D60 (power_up_indices, e.g. 12 instant gang)
// A client's replicas: how many cars and peds are mapped, and how far (in blocks, the largest) the mapped ones are
// from <snap> right now, before it is applied: the drift of one tick of the client's own sim
void ReplicaStats(const Snapshot& snap, int* pCars, int* pPeds, float* pMaxDrift);
void PoolUse(int* pCars, int* pPeds, int* pChars); // live cars, peds, chars in this game
void PrintReplicaLog(); // what the replicas did since the last call (explosions carried out, wrecks and corpses made again...)
// Drivers: how many peds the snapshot has in a car, how many of their cars have a driver here, and how many of
// those are the same (mapped) ped
void DriverStats(const Snapshot& snap, int* pSent, int* pDriven, int* pSame);
// Traffic cars: how many the snapshot has with the shared traffic driver, and how many have it here too
void TrafficDriverStats(const Snapshot& snap, int* pSent, int* pSame);
// GTA2_NET_CARDIFF: where this game's prediction of the local player's car differs from the snapshot's (call after
// restoring the snapshot's tick, before applying it)
void DebugOwnCarDiff(const Snapshot& snap);
// GTA2_NET_COMPARE (the comparison check): the game restored to the snapshot's tick, its fields against the snapshot's,
// tallied; printed (the fields that differ most) and cleared by PrintPredictionStats
void ComparePrediction(const Snapshot& snap);
void PrintPredictionStats(uint32_t frame);
void DebugSoundChannels(uint32_t frame); // GTA2_NET_SOUNDLOG: the sound's playing channels, printed when they change
void DebugAnimLog(uint32_t frame); // GTA2_NET_ANIMLOG: the other players' inputs, states and walking animation, every third frame
void DebugSelfLog(uint32_t frame); // GTA2_NET_SELFLOG: the local player around a death (position, states, the death fields)
std::string DebugPlayerCar(int player, const Snapshot& snap); // for the logs: its car states here and in the snapshot
bool DebugCheckPlayerCars(const char* where); // every in-game player's car is a live car; prints the first time not
void DebugOnCrash(); // from the crash handler (Port/main.cpp): the local player's ped, car and camera
void DebugSetFrame(uint32_t frame); // the client's frame, for the debug checks' messages
// GTA2_NET_MARKER: a square drawn where the newest snapshot has the local player (raw Fix16), over the frame
void SetServerMarker(int32_t x, int32_t y, int32_t z);
void DrawOverlay(); // the game calls it while drawing (Game::Draw_4B92D0, gPortDrawOverlay)
} // namespace engine
} // namespace net
