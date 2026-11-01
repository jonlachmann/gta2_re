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

// GTA2_LOG_FILE: SDL's log, the game's debug strings and message boxes go to that file (on Windows
// a GUI program has no stderr).
static FILE* sLogFile;

static void SDLCALL LogToFile(void*, int, SDL_LogPriority, const char* message)
{
    fprintf(sLogFile, "%s\n", message);
    fflush(sLogFile);
}

int main(int argc, char** argv)
{
    if (const char* logPath = getenv("GTA2_LOG_FILE"))
    {
        sLogFile = fopen(logPath, "w");
        if (sLogFile)
        {
            SDL_SetLogOutputFunction(LogToFile, NULL);
        }
    }
    // Wine drops SDL_* variables before a Windows program sees them.
    if (const char* driver = getenv("GTA2_AUDIO_DRIVER"))
    {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, driver);
    }
    if (const char* file = getenv("GTA2_AUDIO_FILE"))
    {
        SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, file);
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    port_RegisterRenderer();
    port_InitTestInput();

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
