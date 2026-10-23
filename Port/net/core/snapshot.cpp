// Port/net core: see snapshot.hpp. Per entry: the id gap (VarU), an operation, then for a new entity its type and
// a mask of its non-zero fields, for an update the mask of changed fields; the masked values at their widths.
#include "snapshot.hpp"
#include <algorithm>
#include <string.h>

namespace
{
enum Op
{
    kUpdate = 0,
    kCreate = 1,
    kRemove = 2,
};

void WriteValue(net::BitWriter& w, const net::FieldDesc& d, int32_t v)
{
    w.Bits((uint32_t)v, d.bits);
}

int32_t ReadValue(net::BitReader& r, const net::FieldDesc& d)
{
    uint32_t v = r.Bits(d.bits);
    if (d.isSigned && d.bits < 32 && (v >> (d.bits - 1)) & 1)
    {
        v |= ~0u << d.bits; // sign-extend
    }
    return (int32_t)v;
}

// A field mask at the type's field count (up to 64: two words)
void WriteMask(net::BitWriter& w, uint64_t mask, int n)
{
    w.Bits((uint32_t)mask, n < 32 ? n : 32);
    if (n > 32)
    {
        w.Bits((uint32_t)(mask >> 32), n - 32);
    }
}

uint64_t ReadMask(net::BitReader& r, int n)
{
    uint64_t mask = r.Bits(n < 32 ? n : 32);
    if (n > 32)
    {
        mask |= (uint64_t)r.Bits(n - 32) << 32;
    }
    return mask;
}

void WriteFields(net::BitWriter& w, const net::EntityType& t, const net::EntityState& s, uint64_t mask)
{
    WriteMask(w, mask, (int)t.fields.size());
    for (size_t i = 0; i < t.fields.size(); i++)
    {
        if (mask & (1ull << i))
        {
            WriteValue(w, t.fields[i], s.f[i]);
        }
    }
}

bool ReadFields(net::BitReader& r, const net::EntityType& t, net::EntityState& s)
{
    const uint64_t mask = ReadMask(r, (int)t.fields.size());
    for (size_t i = 0; i < t.fields.size(); i++)
    {
        if (mask & (1ull << i))
        {
            s.f[i] = ReadValue(r, t.fields[i]);
        }
    }
    return !r.Overflow();
}

// A value as it arrives: cut to its field width (and sign-extended), so sender and receiver compare the same
int32_t Wire(const net::FieldDesc& d, int32_t v)
{
    if (d.bits >= 32)
    {
        return v;
    }
    uint32_t u = (uint32_t)v & ((1u << d.bits) - 1);
    if (d.isSigned && (u >> (d.bits - 1)) & 1)
    {
        u |= ~0u << d.bits;
    }
    return (int32_t)u;
}

uint64_t ChangedMask(const net::EntityType& t, const net::EntityState& a, const net::EntityState& b)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < t.fields.size(); i++)
    {
        if (Wire(t.fields[i], a.f[i]) != Wire(t.fields[i], b.f[i]))
        {
            mask |= 1ull << i;
        }
    }
    return mask;
}
} // namespace

void net::SortById(Snapshot& snap)
{
    std::sort(snap.entities.begin(), snap.entities.end(), [](const EntityState& a, const EntityState& b) { return a.id < b.id; });
}

void net::WriteDelta(BitWriter& w, const Schema& schema, const Snapshot& from, const Snapshot& to)
{
    static const EntityState kZero = {};
    struct Entry
    {
        uint32_t id;
        int op;
        const EntityState* now;
        uint64_t mask;
    };
    std::vector<Entry> entries;
    size_t i = 0, j = 0;
    while (i < from.entities.size() || j < to.entities.size())
    {
        const EntityState* a = i < from.entities.size() ? &from.entities[i] : NULL;
        const EntityState* b = j < to.entities.size() ? &to.entities[j] : NULL;
        if (b && (!a || b->id < a->id))
        {
            Entry e = {b->id, kCreate, b, ChangedMask(schema.types[b->type], kZero, *b)};
            entries.push_back(e);
            j++;
        }
        else if (a && (!b || a->id < b->id))
        {
            Entry e = {a->id, kRemove, NULL, 0};
            entries.push_back(e);
            i++;
        }
        else
        {
            if (a->type != b->type) // the id was reused by another kind of entity: remove, then create
            {
                Entry e = {b->id, kCreate, b, ChangedMask(schema.types[b->type], kZero, *b)};
                entries.push_back(e);
            }
            else
            {
                const uint64_t mask = ChangedMask(schema.types[b->type], *a, *b);
                if (mask)
                {
                    Entry e = {b->id, kUpdate, b, mask};
                    entries.push_back(e);
                }
            }
            i++;
            j++;
        }
    }
    w.VarU((uint32_t)entries.size());
    uint32_t prev = 0;
    for (size_t k = 0; k < entries.size(); k++)
    {
        const Entry& e = entries[k];
        w.VarU(e.id - prev);
        prev = e.id;
        w.Bits((uint32_t)e.op, 2);
        if (e.op == kCreate)
        {
            w.VarU(e.now->type);
        }
        if (e.op != kRemove)
        {
            WriteFields(w, schema.types[e.now->type], *e.now, e.mask);
        }
    }
}

bool net::ReadDelta(BitReader& r, const Schema& schema, const Snapshot& from, Snapshot& to)
{
    to.entities.clear();
    const uint32_t count = r.VarU();
    if (count > (uint32_t)(from.entities.size() + r.BitsLeft()))
    {
        return false; // more entries than bits: not a delta of ours
    }
    size_t i = 0;
    uint32_t id = 0;
    for (uint32_t k = 0; k < count; k++)
    {
        const uint32_t gap = r.VarU();
        id += gap;
        const int op = (int)r.Bits(2);
        if (r.Overflow() || op > kRemove || (k && !gap)) // ids strictly increase
        {
            return false;
        }
        while (i < from.entities.size() && from.entities[i].id < id) // unchanged ones come along
        {
            to.entities.push_back(from.entities[i++]);
        }
        const EntityState* base = i < from.entities.size() && from.entities[i].id == id ? &from.entities[i] : NULL;
        if (base)
        {
            i++;
        }
        if (op == kRemove)
        {
            if (!base)
            {
                return false;
            }
            continue;
        }
        EntityState s;
        if (op == kCreate)
        {
            memset(&s, 0, sizeof(s));
            s.id = id;
            const uint32_t type = r.VarU();
            if (type >= schema.types.size())
            {
                return false;
            }
            s.type = (uint8_t)type;
        }
        else
        {
            if (!base)
            {
                return false;
            }
            s = *base;
        }
        if (!ReadFields(r, schema.types[s.type], s))
        {
            return false;
        }
        to.entities.push_back(s);
    }
    while (i < from.entities.size())
    {
        to.entities.push_back(from.entities[i++]);
    }
    return !r.Overflow();
}
