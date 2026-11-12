// Entry point: the game folder as working directory and the setup when it has no game data, SDL
// start-up, then the game's WinMain.
#include "port_platform.hpp"
#include "../../setup/setup.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

// GTA2_LOG_FILE is a host path: not mapped into data like the game's drive paths.
#undef fopen

void port_RegisterRenderer();

// GTA2_LOG_FILE: SDL's log, the game's debug strings and message boxes go to that file (on Windows
// a GUI program has no stderr).
static FILE* sLogFile;

static void SDLCALL LogToFile(void*, int, SDL_LogPriority, const char* message)
{
    fprintf(sLogFile, "%s\n", message);
    fflush(sLogFile);
}

static bool IsSetupArg(const char* arg)
{
    return strcmp(arg, "--setup") == 0;
}

// The game folder, and the setup if it has no game data (--setup: the setup screens anyway, to
// replace the data or add the music). Before the game's global constructors, which read the
// registry and data files.
static void EnterGameFolder(int argc, char** argv)
{
    if (const char* logPath = getenv("GTA2_LOG_FILE"))
    {
        sLogFile = fopen(logPath, "w");
        if (sLogFile)
        {
            SDL_SetLogOutputFunction(LogToFile, NULL);
        }
    }
    bool forceSetup = false;
    for (int i = 1; i < argc; i++)
    {
        forceSetup |= IsSetupArg(argv[i]);
    }
    if (!port_PrepareGameFolder(forceSetup))
    {
        exit(0);
    }
}

#if defined(_WIN32)
__attribute__((constructor(101))) static void EnterGameFolderAtStart()
{
    EnterGameFolder(__argc, __argv);
}
#else
// glibc and macOS pass the program's arguments to constructors.
__attribute__((constructor(101))) static void EnterGameFolderAtStart(int argc, char** argv, char**)
{
    EnterGameFolder(argc, argv);
}
#endif

int main(int argc, char** argv)
{
    // Wine drops SDL_* variables before a Windows program sees them.
    if (const char* driver = getenv("GTA2_AUDIO_DRIVER"))
    {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, driver);
    }
    if (const char* file = getenv("GTA2_AUDIO_FILE"))
    {
        SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, file);
    }
    std::string cmdLine;
    for (int i = 1; i < argc; i++)
    {
        if (!IsSetupArg(argv[i]))
        {
            cmdLine += (cmdLine.empty() ? "" : " ");
            cmdLine += argv[i];
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    port_RegisterRenderer();
    port_InitTestInput();

    const int result = WinMain(reinterpret_cast<HINSTANCE>(1), NULL, &cmdLine[0], SW_SHOWNORMAL);
    SDL_Quit();
    return result;
}
