// Port: VC6's rand()/srand(), see rand.hpp.
#include "rand.hpp"
#include "sound.hpp"
#include "state.hpp"
#include "owner.hpp"
#include "../port_globals.hpp"

// 10.5.exe keeps it at 0x626FE0 (a single-threaded CRT): registered, so the network game's rollback keeps it
static unsigned int sState = 1;
PORT_GLOBAL(sState, 0x626FE0);

void port_srand_5EDED7(unsigned int seed)
{
    sState = seed;
}

unsigned int PortRand::State()
{
    return sState;
}

void PortRand::SetState(unsigned int state)
{
    sState = state;
}

unsigned int PortRand::gCalls;
void (*PortRand::gLog)(void* caller);

// The owners' draws (owner.hpp)
unsigned int (*PortRand::gPedKey)(const Ped* p);
unsigned int (*PortRand::gCarKey)(const Car* c);
bool PortRand::gOwned;
void (*PortRand::gTickHook)();
static unsigned int sOwner, sOwnerDraws, sSeed, sTick;

void PortRand::Scope::Enter(unsigned int key)
{
    mActive = key != 0;
    mSavedKey = sOwner, mSavedDraws = sOwnerDraws;
    if (mActive)
    {
        sOwner = key, sOwnerDraws = 0;
    }
}

PortRand::Scope::Scope(unsigned int serviceKey)
{
    Enter(gOwned ? serviceKey : 0);
}

const Ped* PortRand::gLastPed;
const Car* PortRand::gLastCar;

PortRand::Scope::Scope(const Ped* p)
{
    gLastPed = p;
    Enter(gPedKey ? gPedKey(p) : 0);
}

PortRand::Scope::Scope(const Car* c)
{
    gLastCar = c;
    Enter(gCarKey ? gCarKey(c) : 0);
}

PortRand::Scope::~Scope()
{
    if (mActive)
    {
        sOwner = mSavedKey, sOwnerDraws = mSavedDraws;
    }
}

void PortRand::TickStart(unsigned int cycle)
{
    sTick = cycle;
    if (gTickHook)
    {
        gTickHook();
    }
}

void PortRand::SetSeed(unsigned int seed)
{
    sSeed = seed;
}

unsigned int PortRand::Seed()
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
    PortRand::gCalls++;
    if (PortRand::gLog)
    {
        PortRand::gLog(__builtin_return_address(0)); // the divergence check's log of who draws (port/net/engine/dev/compare.cpp)
    }
    if (sOwner)
    {
        return OwnedDraw();
    }
    return PortRand::Step(sState);
}

// sound_obj's own stream (sound.hpp), the same generator
static bool sSoundSeparate;
static unsigned int sSoundState = 1;

void PortRand::SetSoundSeparate(bool separate)
{
    sSoundSeparate = separate;
}

int PortRand::SoundDraw()
{
    if (!sSoundSeparate)
    {
        return port_rand_5EDEE1();
    }
    return Step(sSoundState);
}

