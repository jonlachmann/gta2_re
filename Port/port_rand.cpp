// Port (docs/port.md): VC6's rand()/srand(), see port_rand.h.
#include "port_rand.h"
#include "port_rand_sound.h"
#include "port_rand_state.h"
#include "port_globals.hpp"

// 10.5.exe keeps it at 0x626FE0 (a single-threaded CRT): registered, so the state log compares it
static unsigned int sState = 1;
PORT_GLOBAL(sState, 0x626FE0);

void port_srand_5EDED7(unsigned int seed)
{
    sState = seed;
}

unsigned int PortRandState()
{
    return sState;
}

void PortRandSetState(unsigned int state)
{
    sState = state;
}

unsigned int gPortRandCalls;
void (*gPortRandLog)(void* caller);

int port_rand_5EDEE1()
{
    gPortRandCalls++;
    if (gPortRandLog)
    {
        gPortRandLog(__builtin_return_address(0)); // the divergence check's log of who draws (Port/net/engine/debug.cpp)
    }
    return PortRandStep(sState);
}

// sound_obj's own stream (port_rand_sound.h), the same generator
static bool sSoundSeparate;
static unsigned int sSoundState = 1;

void PortRandSound_Separate(bool separate)
{
    sSoundSeparate = separate;
}

int port_rand_sound()
{
    if (!sSoundSeparate)
    {
        return port_rand_5EDEE1();
    }
    return PortRandStep(sSoundState);
}

