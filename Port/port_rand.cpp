// Port (docs/port.md): VC6's rand()/srand(), see port_rand.hpp.
#include "port_rand.hpp"
#include "port_rand_sound.h"
#include "port_rand_state.h"
#include "port_rand_owner.h"
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

// The owners' draws (port_rand_owner.h)
unsigned int (*gPortRandPedKey)(const Ped* p);
unsigned int (*gPortRandCarKey)(const Car* c);
bool gPortRandOwned;
void (*gPortRandTickHook)();
static unsigned int sOwner, sOwnerDraws, sSeed, sTick;

void PortRandScope::Enter(unsigned int key)
{
    mActive = key != 0;
    mSavedKey = sOwner, mSavedDraws = sOwnerDraws;
    if (mActive)
    {
        sOwner = key, sOwnerDraws = 0;
    }
}

PortRandScope::PortRandScope(unsigned int serviceKey)
{
    Enter(gPortRandOwned ? serviceKey : 0);
}

PortRandScope::PortRandScope(const Ped* p)
{
    Enter(gPortRandPedKey ? gPortRandPedKey(p) : 0);
}

PortRandScope::PortRandScope(const Car* c)
{
    Enter(gPortRandCarKey ? gPortRandCarKey(c) : 0);
}

PortRandScope::~PortRandScope()
{
    if (mActive)
    {
        sOwner = mSavedKey, sOwnerDraws = mSavedDraws;
    }
}

void PortRandTickStart(unsigned int cycle)
{
    sTick = cycle;
    if (gPortRandTickHook)
    {
        gPortRandTickHook();
    }
}

void PortRandSetSeed(unsigned int seed)
{
    sSeed = seed;
}

unsigned int PortRandSeed()
{
    return sSeed;
}

// 15 bits, as rand()'s, from (seed, owner, tick, draw): splitmix64's finalizer
static int OwnedDraw()
{
    unsigned long long x = sSeed * 0x9E3779B97F4A7C15ull ^ sOwner * 0xC2B2AE3D27D4EB4Full ^ sTick * 0x165667B19E3779F9ull ^
                           (sOwnerDraws++ + 1) * 0xD6E8FEB86659FD93ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (int)(x & 0x7FFF);
}

int port_rand_5EDEE1()
{
    gPortRandCalls++;
    if (gPortRandLog)
    {
        gPortRandLog(__builtin_return_address(0)); // the divergence check's log of who draws (Port/net/engine/debug.cpp)
    }
    if (sOwner)
    {
        return OwnedDraw();
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

