/* What Debian 13's static libstdc++ takes from glibc 2.32 and later, for libopenal.so.1 linked against
   glibc 2.31 (deps.sh). Its version script keeps these symbols local. */
#include <stdint.h>
#include <stdlib.h>
#include <sys/random.h>

char __libc_single_threaded = 0;

unsigned long __isoc23_strtoul(const char* s, char** end, int base)
{
    return strtoul(s, end, base);
}

uint32_t arc4random(void)
{
    uint32_t v;
    if (getrandom(&v, sizeof v, 0) == sizeof v)
        return v;
    return (uint32_t)random();
}
