// Port (docs/netcode.md): the particles' random stream, see particle.hpp. Not in rand.cpp: that file's
// headers rename rand, and rng.hpp brings in the C library's.
#include "particle.hpp"
#include "state.hpp"
#undef gRng_6F6784
#include "Source/rng.hpp"

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
    return (s16)(PortRandStep(sParticleState) % max_rnd);
}
