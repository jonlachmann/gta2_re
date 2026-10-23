// Port entry point (docs/port.md), in place of decomp_main's test.cpp: no globals check, no ImGui hook,
// no boot-to-map debug switches; straight into the game's WinMain, whose window is SDL's.
#include "Source/winmain.hpp"
#include <SDL3/SDL_main.h>
#include "headless/fast_clock.hpp"
#include "net/net_main.hpp"
#include "net/engine/engine.hpp"
#include <string>
#include <vector>

#if !defined(_WIN32)
    #include <execinfo.h>
    #include <signal.h>
    #include <stdio.h>
    #if defined(__APPLE__)
        #include <sys/ucontext.h>
    #else
        #include <ucontext.h>
    #endif
    #include <unistd.h>

// A crash prints the faulting address, the instruction and its backtrace to stderr (macOS throttles its crash
// reports), then dies as it would have
static void OnCrash(int sig, siginfo_t* info, void* context)
{
    char line[160];
    unsigned long long pc = 0;
    #if defined(__APPLE__) && defined(__aarch64__)
    pc = ((ucontext_t*)context)->uc_mcontext->__ss.__pc;
    #elif defined(__APPLE__) && defined(__x86_64__)
    pc = ((ucontext_t*)context)->uc_mcontext->__ss.__rip;
    #elif defined(__i386__)
    pc = ((ucontext_t*)context)->uc_mcontext.gregs[REG_EIP];
    #endif
    const int n0 = snprintf(line, sizeof(line), "\ngta2_port: crashed (signal %d) at address %p, instruction 0x%llx; backtrace:\n", sig,
                            info ? info->si_addr : NULL, pc);
    write(2, line, n0 > 0 ? (size_t)n0 : 0);
    void* frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, 2);
    static bool sOnce;
    if (!sOnce) // the netcode's view of the game, not signal-safe but worth it once
    {
        sOnce = true;
        net::engine::DebugOnCrash();
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
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
#endif
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
