#include "ptr_slot.hpp"

#include <unordered_map>
#include <vector>

namespace
{
// Handles are indices plus one, never released: the objects live in the game's pools, so the table
// stays small.
std::vector<void*>& Pointers()
{
    static std::vector<void*> sPointers;
    return sPointers;
}

std::unordered_map<const void*, uint32_t>& Handles()
{
    static std::unordered_map<const void*, uint32_t> sHandles;
    return sHandles;
}
} // namespace

uint32_t PtrSlot_Handle(const void* p)
{
    if (!p)
    {
        return 0;
    }
    auto it = Handles().find(p);
    if (it != Handles().end())
    {
        return it->second;
    }
    Pointers().push_back(const_cast<void*>(p));
    const uint32_t handle = static_cast<uint32_t>(Pointers().size());
    Handles().emplace(p, handle);
    return handle;
}

void* PtrSlot_Pointer(uint32_t handle)
{
    if (handle == 0)
    {
        return nullptr;
    }
    // A value the game never stored, as the file's own: the pointer it would be on a 32-bit target.
    if (handle > Pointers().size())
    {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
    }
    return Pointers()[handle - 1];
}
