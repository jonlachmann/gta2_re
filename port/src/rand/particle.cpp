// Port (port/docs/netcode.md): the particles' random stream, see particle.hpp. Not in rand.cpp: that file's
// headers rename rand, and rng.hpp brings in the C library's.
#include "particle.hpp"
#include "state.hpp"
#undef gRng
#include "rng.hpp"

static bool sParticleSeparate;
static unsigned int sParticleState = 1;
PortRand::ParticleRng PortRand::gParticleRng;

void PortRand::SetParticleSeparate(bool separate)
{
    sParticleSeparate = separate;
}

void PortRand::ParticleMemory(unsigned char** ppAddr, unsigned int* pSize)
{
    *ppAddr = (unsigned char*)&sParticleState;
    *pSize = sizeof(sParticleState);
}

s16 PortRand::ParticleRng::get_int(const s16& max_rnd)
{
    if (!sParticleSeparate)
    {
        return gRng.get_int(max_rnd);
    }
    if (!max_rnd)
    {
        return 0;
    }
    return (s16)(Step(sParticleState) % max_rnd);
}
