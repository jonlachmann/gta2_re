// port/net core (port/docs/netcode.md, "Entities" and "Wire format"): entity states as plain numbers, described by a
// schema (field names and bit widths), and their delta coding against a baseline the receiver has. Which game
// values the fields hold is engine/'s business; this only moves numbers.
#pragma once
#include "bitstream.hpp"
#include <stdint.h>
#include <string>
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
// <check> (GTA2_NET_SNAPCHECK, a dev check): a 16-bit hash of every entity of <to> follows, so the reader can tell
// each one it got wrong (a baseline that isn't the sender's, a field read wrong); older readers ignore it
void WriteDelta(BitWriter& w, const Schema& schema, const Snapshot& from, const Snapshot& to, bool check = false);

// What <to> costs as the one entity of a delta from <from> (NULL: new), in bits: WriteDelta of the two one-entity
// snapshots, without making them (the server prices every entity of every snapshot this way)
size_t EntryBits(const Schema& schema, const EntityState* from, const EntityState& to);

// The receiving side: <from> must be the same baseline the sender used. False on a malformed delta.
// <check>, when the sender added its hashes: each entity of <to> that doesn't match, and how it came (DeltaCheck)
struct DeltaCheck
{
    enum How : uint8_t
    {
        kCarried, // from <from>, unchanged
        kUpdated,
        kCreated,
    };
    bool checked = false; // the sender's hashes were there
    std::vector<size_t> bad; // indices into <to>
    std::vector<uint8_t> how; // per entity of <to>
};
bool ReadDelta(BitReader& r, const Schema& schema, const Snapshot& from, Snapshot& to, DeltaCheck* check = nullptr);
uint16_t EntityHash(const Schema& schema, const EntityState& e);
// " name=value" for each field of <e>, with " (was v)" where <was> (its baseline, or NULL) differs: for the logs
std::string DescribeFields(const Schema& schema, const EntityState& e, const EntityState* was);
} // namespace net
