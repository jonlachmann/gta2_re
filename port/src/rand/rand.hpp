// Port: the game's rand()/srand() are VC6's, so replays and network games play the same everywhere (glibc's and
// macOS's rand are other generators; Windows' UCRT keeps its state per thread). Included under GTA2_PORT by the
// files that call them.
#pragma once

// VC6's: a linear congruential generator, 15 bits per call. Named after 10.5.exe's _rand and _srand.
void port_srand_5EDED7(unsigned int seed);
int port_rand_5EDEE1();

#define srand port_srand_5EDED7
#define rand port_rand_5EDEE1
