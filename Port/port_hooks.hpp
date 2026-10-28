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

// Called last by the crash report (Port/main.cpp, not Windows): the netcode's view of the game, when set
extern void (*gPortCrashHook)();

// ---- Validation and test aids

// The idle time before attract mode in Frontend::Run_4AEDB0: 60 s, or GTA2_PORT_ATTRACT_MS for tests
// (Port/validation/validation.cpp; the same as Scripts/port/statecheck/patch_attract.py does to 10.5.exe)
extern u32 gPortAttractIdleMs;

// ---- The network game (docs/netcode.md)

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
// a network server, which moves a client's cameras' bounds to where that client already shows them (its lag ahead)
extern void (*gPortAfterPlayersHook)();
struct Frontend;
// The frontend's port pages (Port/net/menu.cpp): set up after the game's own (SetupMenuStringsOptionsElements_4B0220),
// and a page's input taken first (UpdatePageFromUserInput_4AE2D0: true, the game's handling is skipped)
extern void (*gPortFrontendSetup)(Frontend* pFrontend);
extern bool (*gPortFrontendInput)(Frontend* pFrontend);
// winmain.cpp, a network game over: the frontend page to go back to (a client that left), or -1 (the game's own)
extern int (*gPortLeaveNetworkGame)();
extern void (*gPortIdleHook)(); // while the game waits for its next frame (Port_PeekMessage): the netcode's polling
extern void (*gPortDrawOverlay)(); // drawn last in Game::Draw_4B92D0; NULL unless a debug overlay is on
extern s16 gPortZoneRoundRobin; // Map_0x370::GetNextZoneOfTypeRoundRobin_4DF0F0's place, rolled back and sent by a network server
// Asked when a network game has ended (winmain.cpp): true to start the next one at once, without the frontend (a
// dedicated server's map change, Port/net/engine/rounds.cpp)
extern bool (*gPortNextNetworkGame)();
// A network client's: its police are off (bSkip_police_67D4F9) but the server's wanted points stand, so Ped's tick
// (Ped.cpp, after ManageWeapon_46F390) neither zeroes them nor registers a criminal (Port/net/engine/client_mode.cpp)
extern bool gPortKeepWantedPoints;
