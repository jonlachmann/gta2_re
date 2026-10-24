// Port/net core: see snapshot.hpp. Per entry: the id gap (VarU), an operation, then for a new entity its type, a mask
// of its non-zero fields and those values at their widths; for an update its changed fields (a mask, or a list of
// indices when shorter) and each changed value at its width or as its difference from the baseline (when shorter).
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

// A field mask: one bit per field of the type, written in 32-bit words at its field count
struct Mask
{
    uint32_t w[net::kMaxFields / 32];
    bool Any() const
    {
        for (size_t i = 0; i < sizeof(w) / sizeof(w[0]); i++)
        {
            if (w[i])
            {
                return true;
            }
        }
        return false;
    }
    bool Has(size_t i) const
    {
        return (w[i / 32] >> (i % 32)) & 1;
    }
    void Set(size_t i)
    {
        w[i / 32] |= 1u << (i % 32);
    }
};

void WriteMask(net::BitWriter& w, const Mask& mask, int n)
{
    for (int i = 0; i < n; i += 32)
    {
        w.Bits(mask.w[i / 32], n - i < 32 ? n - i : 32);
    }
}

Mask ReadMask(net::BitReader& r, int n)
{
    Mask mask = {};
    for (int i = 0; i < n; i += 32)
    {
        mask.w[i / 32] = r.Bits(n - i < 32 ? n - i : 32);
    }
    return mask;
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

int IndexBits(size_t fields)
{
    int b = 1;
    while ((1u << b) < fields)
    {
        b++;
    }
    return b;
}

int VarSBits(int32_t v)
{
    uint32_t z = ((uint32_t)v << 1) ^ (uint32_t)(v >> 31);
    int bits = 8;
    while (z >= 128)
    {
        z >>= 7;
        bits += 8;
    }
    return bits;
}

// A new entity: the mask and the values. An update (base: the client's state): the changed fields as a mask or as a
// list of their indices, whichever is shorter, and each value as itself or as its difference from the base (zigzag
// varint, modulo 2^32), whichever is shorter - half the bits of mask and full values, measured on Tiny Town
void WriteFields(net::BitWriter& w, const net::EntityType& t, const net::EntityState& s, const Mask& mask, const net::EntityState* base)
{
    const size_t n = t.fields.size();
    size_t changed = 0;
    for (size_t i = 0; i < n; i++)
    {
        changed += mask.Has(i);
    }
    if (base)
    {
        const int ib = IndexBits(n);
        const bool list = changed < 128 && 7 + changed * (size_t)ib < n;
        w.Bits(list ? 1 : 0, 1);
        if (list)
        {
            w.Bits((uint32_t)changed, 7);
            for (size_t i = 0; i < n; i++)
            {
                if (mask.Has(i))
                {
                    w.Bits((uint32_t)i, ib);
                }
            }
        }
        else
        {
            WriteMask(w, mask, (int)n);
        }
    }
    else
    {
        WriteMask(w, mask, (int)n);
    }
    for (size_t i = 0; i < n; i++)
    {
        if (!mask.Has(i))
        {
            continue;
        }
        const net::FieldDesc& d = t.fields[i];
        if (base)
        {
            const int32_t diff = (int32_t)((uint32_t)Wire(d, s.f[i]) - (uint32_t)Wire(d, base->f[i]));
            const bool small = VarSBits(diff) < d.bits;
            w.Bits(small ? 1 : 0, 1);
            if (small)
            {
                w.VarS(diff);
                continue;
            }
        }
        WriteValue(w, d, s.f[i]);
    }
}

bool ReadFields(net::BitReader& r, const net::EntityType& t, net::EntityState& s, bool update)
{
    const size_t n = t.fields.size();
    Mask mask = {};
    if (update && r.Bits(1))
    {
        const uint32_t changed = r.Bits(7);
        const int ib = IndexBits(n);
        for (uint32_t k = 0; k < changed && !r.Overflow(); k++)
        {
            const uint32_t i = r.Bits(ib);
            if (i >= n)
            {
                return false;
            }
            mask.Set(i);
        }
    }
    else
    {
        mask = ReadMask(r, (int)n);
    }
    for (size_t i = 0; i < n; i++)
    {
        if (!mask.Has(i))
        {
            continue;
        }
        const net::FieldDesc& d = t.fields[i];
        if (update && r.Bits(1))
        {
            // s holds the base's value here (ReadDelta starts an update from it)
            s.f[i] = Wire(d, (int32_t)((uint32_t)Wire(d, s.f[i]) + (uint32_t)r.VarS()));
            continue;
        }
        s.f[i] = ReadValue(r, d);
    }
    return !r.Overflow();
}

Mask ChangedMask(const net::EntityType& t, const net::EntityState& a, const net::EntityState& b)
{
    Mask mask = {};
    for (size_t i = 0; i < t.fields.size(); i++)
    {
        if (Wire(t.fields[i], a.f[i]) != Wire(t.fields[i], b.f[i]))
        {
            mask.Set(i);
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
        Mask mask;
        const EntityState* was; // its baseline, for an update
    };
    std::vector<Entry> entries;
    size_t i = 0, j = 0;
    while (i < from.entities.size() || j < to.entities.size())
    {
        const EntityState* a = i < from.entities.size() ? &from.entities[i] : NULL;
        const EntityState* b = j < to.entities.size() ? &to.entities[j] : NULL;
        if (b && (!a || b->id < a->id))
        {
            Entry e = {b->id, kCreate, b, ChangedMask(schema.types[b->type], kZero, *b), NULL};
            entries.push_back(e);
            j++;
        }
        else if (a && (!b || a->id < b->id))
        {
            Entry e = {a->id, kRemove, NULL, Mask(), NULL};
            entries.push_back(e);
            i++;
        }
        else
        {
            if (a->type != b->type) // the id was reused by another kind of entity: remove, then create
            {
                Entry e = {b->id, kCreate, b, ChangedMask(schema.types[b->type], kZero, *b), NULL};
                entries.push_back(e);
            }
            else
            {
                const Mask mask = ChangedMask(schema.types[b->type], *a, *b);
                if (mask.Any())
                {
                    Entry e = {b->id, kUpdate, b, mask, a};
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
            WriteFields(w, schema.types[e.now->type], *e.now, e.mask, e.op == kUpdate ? e.was : NULL);
        }
    }
}

size_t net::EntryBits(const Schema& schema, const EntityState* from, const EntityState& to)
{
    static const EntityState kZero = {};
    const EntityType& t = schema.types[to.type];
    BitWriter w;
    if (from && from->type == to.type)
    {
        const Mask mask = ChangedMask(t, *from, to);
        if (!mask.Any())
        {
            w.VarU(0); // no entry
            return w.BitCount();
        }
        w.VarU(1);
        w.VarU(to.id);
        w.Bits(kUpdate, 2);
        WriteFields(w, t, to, mask, from);
        return w.BitCount();
    }
    w.VarU(1); // new, or another kind under the id (removed, then made)
    w.VarU(to.id);
    w.Bits(kCreate, 2);
    w.VarU(to.type);
    WriteFields(w, t, to, ChangedMask(t, kZero, to), NULL);
    return w.BitCount();
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
        if (!ReadFields(r, schema.types[s.type], s, op == kUpdate))
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
