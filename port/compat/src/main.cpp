// Entry point: SDL start-up, the game folder as working directory, then the game's WinMain.
#include "port_platform.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>

void port_RegisterRenderer();

// The game folder: $GTA2_DIR, else the current directory. Entered before the game's global
// constructors, which already read files.
__attribute__((constructor(101))) static void EnterGameFolder()
{
    if (const char* dir = getenv("GTA2_DIR"))
    {
        if (chdir(dir) != 0)
        {
            fprintf(stderr, "Cannot enter GTA2_DIR %s\n", dir);
            exit(1);
        }
    }
}

int main(int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    port_RegisterRenderer();

    std::string cmdLine;
    for (int i = 1; i < argc; i++)
    {
        cmdLine += (i > 1 ? " " : "");
        cmdLine += argv[i];
    }
    const int result = WinMain(reinterpret_cast<HINSTANCE>(1), NULL, &cmdLine[0], SW_SHOWNORMAL);
    SDL_Quit();
    return result;
}
