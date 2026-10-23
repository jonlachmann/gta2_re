// Port (docs/port.md): VC6's rand()/srand(), see port_rand.h.
#include "port_rand.h"
#include "port_rand_sound.h"
#include "port_globals.hpp"
#include "port_rand_particle.h"
#undef gRng_6F6784
#include "Source/rng.hpp"

// 10.5.exe keeps it at 0x626FE0 (a single-threaded CRT): registered, so the state log compares it
static unsigned int sState = 1;
PORT_GLOBAL(sState, 0x626FE0);

void port_srand_5EDED7(unsigned int seed)
{
    sState = seed;
}

// The netcode sends the server's state to clients (Port/net/engine/access.cpp), declared there
unsigned int PortRandState()
{
    return sState;
}

void PortRandSetState(unsigned int state)
{
    sState = state;
}

unsigned int gPortRandCalls; // for the sim bench (Port/headless/sim_bench.cpp): who draws how many

int port_rand_5EDEE1()
{
    gPortRandCalls++;
    sState = sState * 214013u + 2531011u;
    return (int)((sState >> 16) & 0x7FFF);
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
    sSoundState = sSoundState * 214013u + 2531011u;
    return (int)((sSoundState >> 16) & 0x7FFF);
}

// The particles' stream (port_rand_particle.h), the same generator
static bool sParticleSeparate;
static unsigned int sParticleState = 1;
PortParticleRng gPortParticleRng;

void PortRandParticle_Separate(bool separate)
{
    sParticleSeparate = separate;
}

void PortRandParticle_Memory(unsigned char** ppAddr, unsigned int* pSize)
{
    *ppAddr = (unsigned char*)&sParticleState;
    *pSize = sizeof(sParticleState);
}

s16 PortParticleRng::get_int_4F7AE0(const s16& max_rnd)
{
    if (!sParticleSeparate)
    {
        return gRng_6F6784.get_int_4F7AE0(max_rnd);
    }
    if (!max_rnd)
    {
        return 0;
    }
    sParticleState = sParticleState * 214013u + 2531011u;
    return (s16)((int)((sParticleState >> 16) & 0x7FFF) % max_rnd);
}
