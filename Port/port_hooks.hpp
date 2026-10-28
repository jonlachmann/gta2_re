// Port (docs/port.md, "Hooks"): what the game's code (Source/, under GTA2_PORT) calls or reads in the port, in one
// place. Defined in port_hooks.cpp unless noted.
#pragma once

#include <windows.h>

// DirectInput on SDL3 (sdl_dinput.cpp), in place of DirectInputCreateA (input.cpp, InputRecorder_67F8B0.cpp). The
// interfaces are only named, so this doesn't include dinput.h before the game picks its DIRECTINPUT_VERSION.
struct IDirectInputA;
struct IUnknown;
HRESULT WINAPI Port_DirectInputCreateA(HINSTANCE hinst, DWORD dwVersion, IDirectInputA** ppDI, IUnknown* punkOuter);

// The frontend's idle time before attract mode, in ms (Frontend.cpp); 60000 as the game, GTA2_PORT_ATTRACT_MS sets it
// (validation/validation.cpp)
extern unsigned gPortAttractIdleMs;
