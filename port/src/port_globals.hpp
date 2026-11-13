// Every global and static of a GTA2_PORT build registers here with its name, 10.5.exe address and size, and for a
// pointer to a complete type the size of what it points to. The network game saves and restores the world through them
// (port/docs/netcode.md, "The rollback set"; port/net/engine/world.cpp).
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
// The global registered with that 10.5.exe address, or NULL: how port code reaches a file or function static
void* PortGlobalAt(unsigned int ogAddr);
// The same, as a reference; aborts with the address if none is registered there (a renamed or moved static)
void* PortGlobalAtOrDie(unsigned int ogAddr);
template <class T>
inline T& PortStatic(unsigned int ogAddr) // a linear search per use: keep it out of tight loops
{
    return *static_cast<T*>(PortGlobalAtOrDie(ogAddr));
}

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
// A Win32 handle (DECLARE_HANDLE: a struct with only "int unused") is no pointer to data: the compat layer's values are
// its own objects or fakes ((HINSTANCE)1)
template <class T, class = void>
struct PortIsHandle : std::false_type
{
};
template <class T>
struct PortIsHandle<T, decltype(void(&T::unused))> : std::true_type
{
};

template <class T>
inline unsigned int PortPointeeSizeOf(T* const*) // a pointer: the size of what it points to
{
    return PortPointeeSizeImpl<T>(std::integral_constant<bool, PortIsComplete<T>::value && !PortIsHandle<T>::value>());
}

#define PORT_GLOBAL_AS(id, var, addr) \
    static const PortGlobal portGlobal_##id(&var, addr, sizeof(var), #var, PortPointeeSizeOf(&var))
#define PORT_GLOBAL(name, addr) PORT_GLOBAL_AS(name, name, addr)
