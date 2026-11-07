// Port (docs/port.md, "Environment variables"): scripted input for tests, fed into the DirectInput on SDL3
// (sdl_dinput.cpp) as if typed: GTA2_PORT_KEYS=<ms>:<dik>:<1 down|0 up>,... key events at those times after the
// start, GTA2_PORT_PAD=<ms>:<offset>:<value>,... pad events (and the game sees a pad).
#pragma once

#include <windows.h>

// The events due by now, each handed to <add>
void ScriptedInput_Keys(void (*add)(DWORD dik, bool down, DWORD time));
void ScriptedInput_Pad(void (*add)(DWORD ofs, DWORD data));
bool ScriptedInput_HasPad(); // GTA2_PORT_PAD is set
