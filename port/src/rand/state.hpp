// The game's rand() stream (rand.cpp) for code that must not see rand.hpp's renames
// of rand and srand: its state, which the netcode sends to clients (port/net/engine/world_entity.cpp), the count of draws
// (port/net/eval/sim_bench.cpp), and VC6's generator step, which the separate sound and particle streams share.
#pragma once

namespace PortRand
{
unsigned int State();
void SetState(unsigned int state);
extern unsigned int gCalls; // port_rand_5EDEE1 calls so far
extern void (*gLog)(void* caller); // when set, told of each draw's caller

// VC6's rand(): a linear congruential generator, 15 bits per call
// The sound's and the particles' own streams, on in a network game (sound.hpp, particle.hpp), and the particles' state
// for the rollback set
void SetSoundSeparate(bool separate);
void SetParticleSeparate(bool separate);
void ParticleMemory(unsigned char** ppAddr, unsigned int* pSize);

inline int Step(unsigned int& state)
{
    state = state * 214013u + 2531011u;
    return (int)((state >> 16) & 0x7FFF);
}
} // namespace PortRand
