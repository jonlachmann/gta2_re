// Port/net (docs/netcode.md, milestone S): SoundPosEntry::operator new/delete. sound_obj gives every audio entity
// that needs one a SoundPosEntry from the heap; a client restoring an older world would bring back pointers to
// entries freed since. From this fixed pool, which is part of the rollback set, they are restored with the rest.
#include "Source/sound_obj.hpp"
#include "sound_pool.hpp"
#include <stdlib.h>
#include <string.h>

namespace
{
const int kEntries = 1024; // sound_obj has 1020 audio entities, each with at most one

struct Pool
{
    unsigned char mEntries[kEntries][sizeof(SoundPosEntry)];
    int mNext[kEntries]; // free list
    int mHead;
    int mReady; // 0 until the free list is built (the pool is zero-initialised)
} gPool;

void Build()
{
    for (int i = 0; i < kEntries; i++)
    {
        gPool.mNext[i] = i + 1 < kEntries ? i + 1 : -1;
    }
    gPool.mHead = 0;
    gPool.mReady = 1;
}
} // namespace

void* SoundPosEntry::operator new(size_t size)
{
    if (!gPool.mReady)
    {
        Build();
    }
    if (gPool.mHead < 0 || size > sizeof(SoundPosEntry))
    {
        return calloc(1, size); // ponytail: full pool falls back to the heap (not rollback-safe), never seen
    }
    const int i = gPool.mHead;
    gPool.mHead = gPool.mNext[i];
    memset(gPool.mEntries[i], 0, sizeof(SoundPosEntry)); // zeroed like the port's crt::malloc
    return gPool.mEntries[i];
}

void SoundPosEntry::operator delete(void* p)
{
    unsigned char* c = (unsigned char*)p;
    if (c >= gPool.mEntries[0] && c < gPool.mEntries[kEntries])
    {
        const int i = (int)((c - gPool.mEntries[0]) / sizeof(SoundPosEntry));
        gPool.mNext[i] = gPool.mHead;
        gPool.mHead = i;
    }
    else
    {
        free(p);
    }
}

void net::engine::SoundPool_Memory(unsigned char** ppAddr, size_t* pSize)
{
    *ppAddr = (unsigned char*)&gPool;
    *pSize = sizeof(gPool);
}
