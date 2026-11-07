#include "validation.hpp"
#include "Port/port_hooks.hpp"
#include "fast_clock.hpp"
#include "state_log.hpp"
#include "Port/net/eval/sim_bench.hpp"
#include <stdlib.h>

void Validation_Start()
{
#if defined(_WIN32)
    FastClock_Install();
#endif
    // A shorter idle time before attract mode (see Scripts/port/statecheck/patch_attract.py)
    if (const char* ms = getenv("GTA2_PORT_ATTRACT_MS"))
    {
        gPortAttractIdleMs = (u32)atoi(ms);
    }
}

void Validation_VideoInit()
{
    StateLog_Init();
}

void Validation_Frame(unsigned flip)
{
    StateLog_Frame(flip);
    SimBench_Frame(flip); // the netcode's simulation bench (GTA2_SIMBENCH)
}
