// Port (docs/port.md): the game's rand()/srand() are VC6's, so replays play the same everywhere (glibc's
// rand is another generator; Windows' UCRT keeps its state per thread). Included under GTA2_PORT by the
// files that call them.
#pragma once

namespace port_rand_detail
{
inline unsigned int& State()
{
    static unsigned int sState = 1;
    return sState;
}
} // namespace port_rand_detail

inline void port_srand(unsigned int seed)
{
    port_rand_detail::State() = seed;
}

// VC6's: a linear congruential generator, 15 bits per call
inline int port_rand()
{
    unsigned int& s = port_rand_detail::State();
    s = s * 214013u + 2531011u;
    return (int)((s >> 16) & 0x7FFF);
}

#define srand port_srand
#define rand port_rand
