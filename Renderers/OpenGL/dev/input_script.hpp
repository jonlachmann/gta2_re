// Development aids on the game's DirectInput keyboard (dll/dinput_hook.cpp calls them; README "Development tools"):
// GTA2GL_INPUT=<frame>:<dik>[:<frames held>],... feeds keys at those Vid_FlipBuffers counts (dik in hex or decimal),
// GTA2GL_KEYLOG=<file> logs every keyboard event the game reads.
#pragma once
#define DIRECTINPUT_VERSION 0x0700
#include <dinput.h>
#include <windows.h>

// Reads both variables; true when either is set
bool InputScript_Init(const unsigned* pFrameCounter);
bool InputScript_Scripted(); // GTA2GL_INPUT has keys
DWORD InputScript_Due(); // scripted events due by this frame, not read yet
// The next due event into <pData> (<size> bytes); false: none
bool InputScript_Take(LPDIDEVICEOBJECTDATA pData, DWORD size);
// Immediate mode (GetDeviceState): the scripted keys held at this frame set in <keys> (256 bytes)
void InputScript_HoldKeys(BYTE* keys);
void InputScript_LogDropped(const DIDEVICEOBJECTDATA& d); // a lock key event dropped
void InputScript_LogRead(const DIDEVICEOBJECTDATA& d, DWORD left); // an event the game read, <left> more buffered
bool InputScript_Logging();
