// Port/net core (docs/netcode.md, "Net schema" and "Wire format"): entity states as plain numbers, described by a
// schema (field names and bit widths), and their delta coding against a baseline the receiver has. Which game
// values the fields hold is engine/'s business; this only moves numbers.
#pragma once
#include "bitstream.hpp"
#include <stdint.h>
#include <vector>

namespace net
{
const int kMaxFields = 128; // a field mask is up to four 32-bit words

struct FieldDesc
{
    const char* name;
    uint8_t bits; // 1..32
    bool isSigned;
};

struct EntityType
{
    const char* name;
    std::vector<FieldDesc> fields;
};

struct Schema
{
    std::vector<EntityType> types;
};

struct EntityState
{
    uint32_t id; // NetId: unique among the live entities of a snapshot
    uint8_t type; // index into Schema::types
    int32_t f[kMaxFields];
};

struct Snapshot
{
    uint32_t tick = 0;
    std::vector<EntityState> entities; // sorted by id
};

void SortById(Snapshot& snap);

// <to> as changes against <from> (an empty snapshot: everything is new). Entities only in <from> are removed,
// only in <to> are created with all their fields, in both are sent with the fields that changed.
void WriteDelta(BitWriter& w, const Schema& schema, const Snapshot& from, const Snapshot& to);

// What <to> costs as the one entity of a delta from <from> (NULL: new), in bits: WriteDelta of the two one-entity
// snapshots, without making them (the server prices every entity of every snapshot this way)
size_t EntryBits(const Schema& schema, const EntityState* from, const EntityState& to);

// The receiving side: <from> must be the same baseline the sender used. False on a malformed delta.
bool ReadDelta(BitReader& r, const Schema& schema, const Snapshot& from, Snapshot& to);
} // namespace net
