// The network game's hooks (port/docs/netcode.md, "The game's code"): what the game's code (Source/, under GTA2_PORT),
// compat/ and port/src/ call or read for it, in one place; port/net/ sets them. Each is NULL, or the original's value,
// until a network game starts.
#pragma once
#include "types.hpp"

class Sprite;
struct Frontend;

// Called last by the crash report (compat/src/main.cpp): the network game's view of the game, when set
extern void (*gPortCrashHook)();
// After a sprite moved (Sprite::InvalidateZColAndBBoxes, every setter of its place calls it); NULL but for a check
// (GTA2_NET_CHECKGRID: one moved off the map)
extern void (*gPortSpriteMovedHook)(Sprite*);
// While the game waits for its next step: the netcode's polling, so a ping is answered at once and neither a step nor
// a frame wait adds to the round trip
extern void (*gPortIdleHook)();

// No audio device for the game: sound_obj then doesn't run at all. A network server's: nobody hears it, and its
// listener follows slot 0's camera, whose ped (once that player left) can be any ped's, in any car
extern bool gPortNoAudio;
extern s32 gPortTrafficCap; // the ambient traffic cars per player: 16, or a network server's --traffic
// The players the ambient traffic and peds are for (Car.cpp, Char_Pool.cpp: the game's limits are for the whole game, and
// the first players' cameras took them all); 1 but on a network server
extern s32 gPortSpawnPlayers;
// The ambient traffic cars for the whole game (Car.cpp, in place of the original's 16)
inline s32 PortTrafficLimit()
{
    return gPortTrafficCap * gPortSpawnPlayers;
}
// Called in Game::UpdateGame after the players' tick (their cameras moved), before anything spawns; NULL but on a
// network server, which moves a client's cameras' bounds to where that client already shows them (its lag ahead): a car
// or ped spawned out of sight here would come into a fast client's view
extern void (*gPortAfterPlayersHook)();
// Asked in Game::UpdateGame after the players' tick: true, and nothing else runs this tick but the HUD (a network
// game's results: the world stands still, the chat goes on)
extern bool (*gPortWorldStill)();
// The frontend's network page (port/net/menu.cpp): set up after the game's own (SetupMenuStringsOptionsElements),
// and its input taken first (UpdatePageFromUserInput: true, the game's handling is skipped)
extern void (*gPortFrontendSetup)(Frontend* pFrontend);
extern bool (*gPortFrontendInput)(Frontend* pFrontend);
// winmain.cpp, a network game over: the frontend page to go back to (a client that left), or -1 (the game's own)
extern int (*gPortLeaveNetworkGame)();
extern void (*gPortDrawOverlay)(); // drawn last in Game::Draw; NULL unless the netcode draws over the frame
// Asked when a network game has ended (winmain.cpp): true to start the next one at once, without the frontend (a
// dedicated server's map change)
extern bool (*gPortNextNetworkGame)();
// A network client's: its police are off (bSkip_police) but the server's wanted points stand, so Ped's tick neither
// zeroes them nor registers a criminal: zeroed each tick, the HUD would show no cop heads and the cops chasing a player
// would give up between snapshots
extern bool gPortKeepWantedPoints;
// A network client's: the HUD's kill notices (Hud_MpMessage::AnnounceKill) are the server's deaths only, not the ones
// its prediction starts and a restore undoes
extern bool gPortServerKillNotices;
// A network client's "Leave the game? Y / N" is up: a gamepad's A and B answer it as they do the game's quit message
// (port_QuitMessageShown, compat/src/pad.cpp)
extern bool gPortLeavePromptShown;
