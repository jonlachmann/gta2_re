// Port (docs/port.md): the game's rand()/srand() are VC6's, so replays play the same everywhere (glibc's
// rand is another generator; Windows' UCRT keeps its state per thread). Included under GTA2_PORT by the
// files that call them.
#pragma once

// VC6's: a linear congruential generator, 15 bits per call. Out of line, named after 10.5.exe's _rand, so
// the call trace (Scripts/port/state_check.py TRACE=...:_rand) can follow it in both exes.
void port_srand_5EDED7(unsigned int seed);
int port_rand_5EDEE1();

#define srand port_srand_5EDED7
#define rand port_rand_5EDEE1
