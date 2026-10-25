// Port (docs/port.md): the port's hooks in the game's code, declared once. The Source/ files that use them include
// this under #if defined(GTA2_PORT), so VC6 never sees it; Port/ defines and sets them.
#pragma once
#include "Source/types.hpp"
#include <stddef.h>
#include <windows.h>

// DirectInput on SDL3 (Port/sdl_dinput.cpp), for input.cpp and InputRecorder_67F8B0.cpp
HRESULT WINAPI Port_DirectInputCreateA(HINSTANCE hinst, DWORD dwVersion, struct IDirectInputA** ppDI, struct IUnknown* punkOuter);

namespace crt
{
// Source/crt_stubs.cpp calls it on every allocation (size) and free (0) when set: the sim bench lists heap use during
// ticks (Port/headless/neteval.cpp)
extern void (*gPortHeapHook)(size_t size);
} // namespace crt

// The idle time before attract mode in Frontend::Run_4AEDB0: 60 s, or GTA2_PORT_ATTRACT_MS for tests (Port/main.cpp; the
// same as Scripts/port/patch_attract.py does to 10.5.exe)
extern u32 gPortAttractIdleMs;

// Port/net/net_main.cpp's
extern s32 gPortTrafficCap; // the ambient traffic cars per player (Car.cpp): 16, or a network server's --traffic
// Port/net/engine/players.cpp's: the players the ambient traffic and peds are for (Car.cpp, Char_Pool.cpp: the game's
// limits are for the whole game, and the first players' cameras took them all); 1 but on a network server
extern s32 gPortSpawnPlayers;
// Called in Game::UpdateGame_4B9410 after the players' tick (their cameras moved), before anything spawns; NULL but on
// a network server, which moves a client's cameras' bounds to where that client already shows them (its lag ahead)
extern void (*gPortAfterPlayersHook)();
extern void (*gPortIdleHook)(); // while the game waits for its next frame (Port_PeekMessage): the netcode's polling
extern void (*gPortDrawOverlay)(); // drawn last in Game::Draw_4B92D0; NULL unless a debug overlay is on
extern s16 gPortZoneRoundRobin; // Map_0x370::GetNextZoneOfTypeRoundRobin_4DF0F0's place, rolled back and sent by a network server
// Port/net/engine/netplay.cpp's: asked when a network game has ended (winmain.cpp): true to start the next one at once,
// without the frontend (a dedicated server's map change, Port/net/engine/rounds.cpp)
extern bool (*gPortNextNetworkGame)();
// A network client's: its police are off (bSkip_police_67D4F9) but the server's wanted points stand, so Ped's tick
// (Ped.cpp, after ManageWeapon_46F390) neither zeroes them nor registers a criminal (Port/net/engine/client_mode.cpp)
extern bool gPortKeepWantedPoints;
