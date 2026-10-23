// Port/net (docs/netcode.md, "Code layout"): what the session sees of the game. Plain types only: the decomp's
// types stay inside engine/, so the session and modes don't move when the decomp does.
#pragma once
#include "../core/snapshot.hpp"
#include <stdint.h>
#include <string>

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
// Puts a snapshot's players into this game (cars and peds: later)
void Apply(const Snapshot& snap, int localPlayer);
// The position of a player's ped or car, in blocks; false without a ped
bool PlayerPosition(int player, float* pX, float* pY);

// What a client doesn't simulate (client_mode.cpp): spawning and despawning, scripts, police; called every frame
void ClientModeFrame();
} // namespace engine
} // namespace net
