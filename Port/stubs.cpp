// Port build: replacements for the parts of the decomp_main build it leaves out (docs/port.md).
#include "Source/ImGuiDebug.hpp"

// The ImGui overlay draws through GTA2Hax's Direct3D 9 renderer, which the port doesn't use.
void CC ImGuiDebugDraw()
{
}

// decomp_main's entry point (test.cpp) turns off traffic lights, trains and more through this for its
// "boot to map" debugging. The port plays the full game.
void EnableBoot2MapDebugOptions()
{
}
