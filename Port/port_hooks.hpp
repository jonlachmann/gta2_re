// Port (docs/port.md, "`Source/` changes"): what the game's code (Source/, under GTA2_PORT) calls or reads in the port,
// in one place. The Source/ files include this under #if defined(GTA2_PORT), so VC6 never sees it; Port/ defines and sets
// them.
#pragma once
#include "Source/types.hpp"
#include <stddef.h>
#include <windows.h>

// ---- The platform layer

// DirectInput on SDL3 (Port/sdl_dinput.cpp), in place of DirectInputCreateA (input.cpp, InputRecorder_67F8B0.cpp). The
// interfaces are only named, so this doesn't include dinput.h before the game picks its DIRECTINPUT_VERSION.
HRESULT WINAPI Port_DirectInputCreateA(HINSTANCE hinst, DWORD dwVersion, struct IDirectInputA** ppDI, struct IUnknown* punkOuter);
// The character a key's last press typed, in the system's keyboard layout with shift (Latin-1, 0 none), for the chat
// line (Hud.cpp): the game's tables (data/keyboard) are per key without shift, so a '/' behind shift never came
u16 Port_TypedChar(s32 dik);

// Called last by the crash report (Port/main.cpp, not Windows): the netcode's view of the game, when set (a hook, so the
// platform layer doesn't call into the netcode)
extern void (*gPortCrashHook)();
// After a sprite moved (Sprite::InvalidateZColAndBBoxes_59E7B0, every setter of its place calls it); NULL but for a check
// (GTA2_NET_CHECKGRID: one moved off the map)
extern void (*gPortSpriteMovedHook)(class Sprite*);

// ---- Validation and test aids

// The idle time before attract mode in Frontend::Run_4AEDB0: 60 s, or GTA2_PORT_ATTRACT_MS for tests
// (Port/validation/validation.cpp; the same as Scripts/port/statecheck/patch_attract.py does to 10.5.exe)
extern u32 gPortAttractIdleMs;

// ---- The network game (docs/netcode.md)

// No audio device for the game (Port/sdl_miles.cpp's AIL_waveOutOpen fails): sound_obj then doesn't run at all. A
// network server's: nobody hears it, and its listener follows slot 0's camera, whose ped (once that player left) can
// be any ped's, in any car (the sound read a freed one: a crash)
extern bool gPortNoAudio;
extern s32 gPortTrafficCap; // the ambient traffic cars per player: 16, or a network server's --traffic (Port/net/net_main.cpp)
// The players the ambient traffic and peds are for (Car.cpp, Char_Pool.cpp: the game's limits are for the whole game, and
// the first players' cameras took them all); 1 but on a network server (Port/net/engine/players.cpp)
extern s32 gPortSpawnPlayers;
// The ambient traffic cars for the whole game (Car.cpp, in place of the original's 16)
inline s32 PortTrafficLimit()
{
    return gPortTrafficCap * gPortSpawnPlayers;
}
// Called in Game::UpdateGame_4B9410 after the players' tick (their cameras moved), before anything spawns; NULL but on
// a network server, which moves a client's cameras' bounds to where that client already shows them (its lag ahead): a car
// or ped spawned out of sight here would come into a fast client's view
extern void (*gPortAfterPlayersHook)();
// Asked in Game::UpdateGame_4B9410 after the players' tick: true, and nothing else runs this tick but the HUD (a network
// game's results: the world stands still, the chat goes on; Port/net/engine/netplay.cpp)
extern bool (*gPortWorldStill)();
struct Frontend;
// The frontend's port pages (Port/net/menu.cpp): set up after the game's own (SetupMenuStringsOptionsElements_4B0220),
// and a page's input taken first (UpdatePageFromUserInput_4AE2D0: true, the game's handling is skipped)
extern void (*gPortFrontendSetup)(Frontend* pFrontend);
extern bool (*gPortFrontendInput)(Frontend* pFrontend);
// winmain.cpp, a network game over: the frontend page to go back to (a client that left), or -1 (the game's own)
extern int (*gPortLeaveNetworkGame)();
// While the game waits for its next frame (Port_PeekMessage): the netcode's polling, so a ping is answered at once and
// neither a tick nor a frame wait adds to the round trip
extern void (*gPortIdleHook)();
extern void (*gPortDrawOverlay)(); // drawn last in Game::Draw_4B92D0; NULL unless a debug overlay is on
// Map_0x370::GetNextZoneOfTypeRoundRobin_4DF0F0's place, rolled back and sent by a network server: as the original's
// function static it is outside the rollback set, and a client would respawn at another restart zone than the server's
extern s16 gPortZoneRoundRobin;
// Asked when a network game has ended (winmain.cpp): true to start the next one at once, without the frontend (a
// dedicated server's map change, Port/net/engine/rounds.cpp)
extern bool (*gPortNextNetworkGame)();
// A network client's: its police are off (bSkip_police_67D4F9) but the server's wanted points stand, so Ped's tick
// (Ped.cpp, after ManageWeapon_46F390) neither zeroes them nor registers a criminal (Port/net/engine/client_mode.cpp):
// zeroed each tick, the HUD would show no cop heads and the cops chasing a player would give up between snapshots
extern bool gPortKeepWantedPoints;
// A network client's: the HUD's kill notices (Hud_MpMessage::AnnounceKill_5D5770) are the server's deaths only
// (Port/net/engine/players.cpp), not the ones its prediction starts and a restore undoes (each resimulation announced
// those again, with whoever the prediction had as the killer)
extern bool gPortServerKillNotices;
