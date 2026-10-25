// Port (docs/port.md): the game's rand() stream (port_rand.cpp) for code that must not see port_rand.h's renames
// of rand and srand: its state, which the netcode sends to clients (Port/net/engine/access.cpp), the count of draws
// (Port/headless/sim_bench.cpp), and VC6's generator step, which the separate sound and particle streams share.
#pragma once

unsigned int PortRandState();
void PortRandSetState(unsigned int state);
extern unsigned int gPortRandCalls; // port_rand_5EDEE1 calls so far
extern void (*gPortRandLog)(void* caller); // when set, told of each draw's caller

// VC6's rand(): a linear congruential generator, 15 bits per call
inline int PortRandStep(unsigned int& state)
{
    state = state * 214013u + 2531011u;
    return (int)((state >> 16) & 0x7FFF);
}
