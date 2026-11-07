#pragma once
#include "types.hpp"

// PTR32(T): a pointer kept in 4 bytes. GTA2's script data has 4-byte slots where the running script keeps pointers
// to game objects (SCR_POINTER and others); in the 64-bit port (docs/port.md) a pointer doesn't fit, so the slot
// holds a handle into a table instead (Port/ptr_table.cpp). Everywhere else it is a plain T*.
#if defined(GTA2_PORT_64)
void* PtrTable_Get(u32 handle);
u32 PtrTable_Put(const void* p); // 0 for null

template <class T>
struct ptr32
{
    u32 mHandle;

    operator T*() const
    {
        return (T*)PtrTable_Get(mHandle);
    }
    T* operator->() const
    {
        return (T*)PtrTable_Get(mHandle);
    }
    ptr32& operator=(T* p)
    {
        mHandle = PtrTable_Put(p);
        return *this;
    }
};
    #define PTR32(T) ptr32<T>
#else
    #define PTR32(T) T*
#endif
