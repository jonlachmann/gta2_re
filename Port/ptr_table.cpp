// Port (docs/port.md): the handles behind PTR32 (Source/ptr32.hpp) in the 64-bit build. Every pointer the scripts
// keep gets one, for good: they point to pool slots and a few globals, so the table stays small.
#include "Source/types.hpp"
#if defined(GTA2_PORT_64)
#include "Source/ptr32.hpp"
#include <unordered_map>
#include <vector>

namespace
{
std::vector<void*>& Pointers()
{
    static std::vector<void*> sPointers(1, (void*)0); // handle 0 is null
    return sPointers;
}
std::unordered_map<const void*, u32>& Handles()
{
    static std::unordered_map<const void*, u32> sHandles;
    return sHandles;
}
} // namespace

void* PtrTable_Get(u32 handle)
{
    return handle < Pointers().size() ? Pointers()[handle] : 0;
}

u32 PtrTable_Put(const void* p)
{
    if (!p)
    {
        return 0;
    }
    std::unordered_map<const void*, u32>::iterator it = Handles().find(p);
    if (it != Handles().end())
    {
        return it->second;
    }
    const u32 handle = (u32)Pointers().size();
    Pointers().push_back((void*)p);
    Handles()[p] = handle;
    return handle;
}
#endif
