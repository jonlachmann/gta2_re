// Port (docs/port.md): VC6's rand()/srand(), see port_rand.hpp.
#include "port_rand.hpp"
#include "port_globals.hpp"

// 10.5.exe keeps it at 0x626FE0 (a single-threaded CRT): registered, so the state log compares it
static unsigned int sState = 1;
PORT_GLOBAL(sState, 0x626FE0);

void port_srand_5EDED7(unsigned int seed)
{
    sState = seed;
}

int port_rand_5EDEE1()
{
    sState = sState * 214013u + 2531011u;
    return (int)((sState >> 16) & 0x7FFF);
}
