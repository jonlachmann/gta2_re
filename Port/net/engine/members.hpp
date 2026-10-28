// Port/net (docs/netcode.md, "Net schema"): a structure's members on the wire, by name and offset (the 64-bit layout
// isn't the original's, so never by position). The car's physics (cars.cpp), its AI (car_ai.cpp) and a ped's AI
// state and char (ped_state.cpp) are such tables.
#pragma once
#include "../core/snapshot.hpp"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>

namespace net
{
namespace engine
{
struct Member
{
    const char* name;
    size_t offset;
    uint8_t size; // bytes: 1, 2 or 4
    bool isSigned;
};
#define NET_MEMBER(type, member, size, isSigned) {#member, offsetof(type, member), size, isSigned}

inline int32_t GetMember(const void* object, const Member& d)
{
    const unsigned char* a = (const unsigned char*)object + d.offset;
    if (d.size == 1)
    {
        return d.isSigned ? (int32_t) * (const int8_t*)a : (int32_t) * (const uint8_t*)a;
    }
    if (d.size == 2)
    {
        int16_t s;
        uint16_t u;
        memcpy(&s, a, 2);
        memcpy(&u, a, 2);
        return d.isSigned ? s : u;
    }
    int32_t v;
    memcpy(&v, a, 4);
    return v;
}

inline void SetMember(void* object, const Member& d, int32_t v)
{
    unsigned char* a = (unsigned char*)object + d.offset;
    if (d.size == 1)
    {
        *a = (unsigned char)v;
    }
    else if (d.size == 2)
    {
        const int16_t s = (int16_t)v;
        memcpy(a, &s, 2);
    }
    else
    {
        memcpy(a, &v, 4);
    }
}

// A table's schema fields, each at its member's width
inline void AppendFields(const Member* table, size_t count, std::vector<FieldDesc>& out)
{
    for (size_t i = 0; i < count; i++)
    {
        out.push_back(FieldDesc{table[i].name, (uint8_t)(table[i].size * 8), table[i].isSigned});
    }
}

// The members into an entity's fields from first on, and back
inline void CaptureMembers(const void* object, const Member* table, size_t count, EntityState& e, int first)
{
    for (size_t i = 0; i < count; i++)
    {
        e.f[first + i] = GetMember(object, table[i]);
    }
}

inline void ApplyMembers(void* object, const Member* table, size_t count, const EntityState& e, int first)
{
    for (size_t i = 0; i < count; i++)
    {
        SetMember(object, table[i], e.f[first + i]);
    }
}

// A member's place in its table (-1: not in it), for the code that reads one of them
inline int MemberIndex(const Member* table, size_t count, size_t offset)
{
    for (size_t i = 0; i < count; i++)
    {
        if (table[i].offset == offset)
        {
            return (int)i;
        }
    }
    return -1;
}
} // namespace engine
} // namespace net
