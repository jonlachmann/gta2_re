// Port (docs/netcode.md): the particles' random numbers (ParticleManager.cpp, Particle_4C.cpp). By default they come from
// the game's one stream, as in 10.5.exe. With PortRandParticle_Separate(true) (the netcode) particles draw from a
// stream of their own, part of the rollback set: a resimulation makes the same flames and smoke as the frame before,
// whatever the sim drew, and the sim's own draws don't depend on the particles.
#pragma once
#include "Source/types.hpp"

struct PortParticleRng
{
    s16 get_int_4F7AE0(const s16& max_rnd); // rng::get_int_4F7AE0
};
extern PortParticleRng gPortParticleRng;
void PortRandParticle_Separate(bool separate);
void PortRandParticle_Memory(unsigned char** ppAddr, unsigned int* pSize); // for the rollback set

// After every other include: the particle code's gRng_6F6784.get_int_4F7AE0 calls this one
#define gRng_6F6784 gPortParticleRng
