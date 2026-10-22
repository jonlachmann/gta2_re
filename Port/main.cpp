// Port entry point (docs/port.md), in place of decomp_main's test.cpp: no globals check, no ImGui hook,
// no boot-to-map debug switches; straight into the game's WinMain, whose window is SDL's.
#include "Source/winmain.hpp"
#include <SDL3/SDL_main.h>
#include <string>

int main(int argc, char** argv)
{
    std::string cmdLine;
    for (int i = 1; i < argc; i++)
    {
        cmdLine += (i > 1 ? " " : "");
        cmdLine += argv[i];
    }
    const int ret = WinMain_5E53F0(GetModuleHandleA(NULL), NULL, &cmdLine[0], SW_SHOWNORMAL);
    SDL_Quit();
    return ret;
}
