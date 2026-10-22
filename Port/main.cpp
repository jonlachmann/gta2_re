// Port entry point (docs/port.md), in place of decomp_main's test.cpp: no globals check, no ImGui hook,
// no boot-to-map debug switches; straight into the game's WinMain.
#include "Source/winmain.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return WinMain_5E53F0(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}
