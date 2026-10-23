// Port/net (docs/netcode.md, milestone S): the SoundPosEntry pool, as blocks for the rollback set.
#pragma once
#include <stddef.h>

namespace net
{
namespace engine
{
// The pool's memory: the entries, the free list and its head (world.cpp adds them to the rollback set)
void SoundPool_Memory(unsigned char** ppAddr, size_t* pSize);
} // namespace engine
} // namespace net
