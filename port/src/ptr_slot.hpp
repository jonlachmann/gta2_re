// A pointer kept in a 4-byte slot of a game file's data (the .scr script's object variables), for
// 64-bit targets: the slot holds a handle, a table holds the pointer.
#pragma once

#include <stdint.h>

uint32_t PtrSlot_Handle(const void* p);
void* PtrSlot_Pointer(uint32_t handle);

template <typename T> struct PtrSlot32
{
    uint32_t mHandle;

    operator T*() const
    {
        return static_cast<T*>(PtrSlot_Pointer(mHandle));
    }

    T* operator->() const
    {
        return static_cast<T*>(PtrSlot_Pointer(mHandle));
    }

    PtrSlot32& operator=(T* p)
    {
        mHandle = PtrSlot_Handle(p);
        return *this;
    }
};
