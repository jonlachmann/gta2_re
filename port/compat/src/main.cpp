// Entry point: the game folder as working directory and the setup when it has no game data, SDL
// start-up, then the game's WinMain.
#include "port_platform.hpp"
#include "../../setup/setup.hpp"
#include "reg2_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../net/net_main.hpp"
#include "../../src/net_hooks.hpp" // gPortCrashHook
#if !defined(_WIN32)
    #include <execinfo.h>
    #include <signal.h>
    #include <unistd.h>
#endif

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
    // Before the setup's windows and the game's: on Linux the id matches the packages' desktop file.
    SDL_SetAppMetadata("reG2", GTA2_VERSION, GTA2_APP_ID);
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

#if !defined(_WIN32)
// A crash prints the faulting address and its backtrace to stderr (macOS throttles its crash reports), and the network
// game's view of the game, then dies as it would have
static void OnCrash(int sig, siginfo_t* info, void*)
{
    char line[160];
    const int n0 =
        snprintf(line, sizeof(line), "\nreg2: crashed (signal %d) at address %p; backtrace:\n", sig, info ? info->si_addr : NULL);
    write(2, line, n0 > 0 ? (size_t)n0 : 0);
    void* frames[64];
    backtrace_symbols_fd(frames, backtrace(frames, 64), 2);
    static bool sOnce;
    if (!sOnce && gPortCrashHook) // not signal-safe, but worth it once
    {
        sOnce = true;
        gPortCrashHook();
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

int main(int argc, char** argv)
{
#if !defined(_WIN32)
    struct sigaction sa = {};
    sa.sa_sigaction = OnCrash;
    sa.sa_flags = SA_SIGINFO;
    for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGTRAP, SIGILL, SIGFPE})
    {
        sigaction(sig, &sa, NULL);
    }
#endif
    // Wine drops SDL_* variables before a Windows program sees them.
    if (const char* driver = getenv("GTA2_AUDIO_DRIVER"))
    {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, driver);
    }
    if (const char* file = getenv("GTA2_AUDIO_FILE"))
    {
        SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, file);
    }
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++)
    {
        if (!IsSetupArg(argv[i]))
        {
            args.push_back(argv[i]);
        }
    }
    const int net = NetMain_Init(args); // --server, --connect (port/docs/netcode.md)
    if (net < 0)
    {
        return 1;
    }
    if (net > 0)
    {
        args.push_back("-N"); // the game's switch for a network game
    }
    std::string cmdLine;
    for (const std::string& arg : args)
    {
        cmdLine += (cmdLine.empty() ? "" : " ");
        cmdLine += arg;
    }

    if (port_Headless())
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    // Headless: no gamepads, as their every event poll asks the system for new ones
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | (port_Headless() ? 0 : SDL_INIT_GAMEPAD)))
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
