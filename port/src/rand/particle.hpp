// Port (port/docs/netcode.md): the particles' random numbers (ParticleManager.cpp, Particle_4C.cpp). By default they come from
// the game's one stream, as in 10.5.exe. With PortRand::SetParticleSeparate(true) (the netcode) particles draw from a
// stream of their own, part of the rollback set: a resimulation makes the same flames and smoke as the frame before,
// whatever the sim drew, and the sim's own draws don't depend on the particles.
#pragma once
#include "types.hpp"

namespace PortRand
{
struct ParticleRng
{
    s16 get_int(const s16& max_rnd); // rng::get_int
};
extern ParticleRng gParticleRng;
} // namespace PortRand

// After every other include: the particle code's gRng.get_int calls this one
#define gRng PortRand::gParticleRng
