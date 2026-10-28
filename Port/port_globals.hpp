// Port (docs/port.md): every DEFINE_GLOBAL of a GTA2_PORT build registers here with its name, 10.5.exe
// address and size, and for a pointer to a complete type the size of what it points to. The state log
// (Port/validation/state_log.cpp) compares them with 10.5.exe's memory, frame by frame.
#pragma once

#include <stddef.h>
#include <type_traits>

struct PortGlobal
{
    PortGlobal(const void* pVar, unsigned int ogAddr, unsigned int size, const char* pName, unsigned int pointeeSize);
    const void* mVar;
    unsigned int mOgAddr;
    unsigned int mSize;
    const char* mName;
    unsigned int mPointeeSize; // 0: not a pointer to something complete
};

// All registered globals (in the order their files initialise)
const PortGlobal* const* PortGlobals(size_t* pCount);

template <class T, class = void>
struct PortIsComplete : std::false_type
{
};
template <class T>
struct PortIsComplete<T, decltype(void(sizeof(T)))> : std::true_type
{
};

template <class T>
inline unsigned int PortPointeeSizeImpl(std::true_type)
{
    return (unsigned int)sizeof(T);
}
template <class T>
inline unsigned int PortPointeeSizeImpl(std::false_type) // incomplete, void or a function
{
    return 0;
}

template <class T>
inline unsigned int PortPointeeSizeOf(const T*) // not a pointer
{
    return 0;
}
template <class T>
inline unsigned int PortPointeeSizeOf(T* const*) // a pointer: the size of what it points to
{
    return PortPointeeSizeImpl<T>(PortIsComplete<T>());
}

#define PORT_GLOBAL(name, addr) \
    static const PortGlobal portGlobal_##name(&name, addr, sizeof(name), #name, PortPointeeSizeOf(&name))
