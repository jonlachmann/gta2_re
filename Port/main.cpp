// Port entry point (docs/port.md), in place of decomp_main's test.cpp: no globals check, no ImGui hook,
// no boot-to-map debug switches; straight into the game's WinMain, whose window is SDL's.
#include "Source/winmain.hpp"
#include <SDL3/SDL_main.h>
#include "headless/fast_clock.hpp"
#include "net/net_main.hpp"
#include <string>
#include <vector>

int main(int argc, char** argv)
{
#if defined(_WIN32)
    FastClock_Install(); // GTA2_SPEED (docs/port.md, M2)
#endif
    std::vector<std::string> args(argv + 1, argv + argc);
    const int net = NetMain_Init(args); // --server, --connect (docs/netcode.md)
    if (net < 0)
    {
        return 1;
    }
    if (net > 0)
    {
        args.push_back("-N"); // the game's switch for a network game
    }
    std::string cmdLine;
    for (size_t i = 0; i < args.size(); i++)
    {
        cmdLine += (i ? " " : "");
        cmdLine += args[i];
    }
    const int ret = WinMain_5E53F0(GetModuleHandleA(NULL), NULL, &cmdLine[0], SW_SHOWNORMAL);
    SDL_Quit();
    return ret;
}
