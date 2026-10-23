// Port/net core tests (Scripts/port/net_tests.sh): no game, no network; a failed CHECK prints and exits 1.
#include "../core/bitstream.hpp"
#include "../core/protocol.hpp"
#include "../core/snapshot.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) \
    do \
    { \
        if (!(c)) \
        { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            exit(1); \
        } \
    } while (0)

namespace
{
unsigned gSeed = 1;
uint32_t Rand()
{
    gSeed = gSeed * 1103515245u + 12345u;
    return (gSeed >> 8) ^ (gSeed << 13);
}

void TestBits()
{
    net::BitWriter w;
    std::vector<uint32_t> values, widths;
    for (int i = 0; i < 2000; i++)
    {
        const uint32_t n = 1 + Rand() % 32;
        const uint32_t v = n == 32 ? Rand() : Rand() & ((1u << n) - 1);
        w.Bits(v, (int)n);
        values.push_back(v);
        widths.push_back(n);
    }
    w.VarU(0);
    w.VarU(127);
    w.VarU(128);
    w.VarU(0xFFFFFFFFu);
    w.VarS(-1);
    w.VarS(-2147483647 - 1);
    w.VarS(12345);
    w.Bool(true);
    const char text[] = "gta2";
    w.Bytes(text, 4);
    net::BitReader r(w.Data().data(), w.ByteCount());
    for (size_t i = 0; i < values.size(); i++)
    {
        CHECK(r.Bits((int)widths[i]) == values[i]);
    }
    CHECK(r.VarU() == 0);
    CHECK(r.VarU() == 127);
    CHECK(r.VarU() == 128);
    CHECK(r.VarU() == 0xFFFFFFFFu);
    CHECK(r.VarS() == -1);
    CHECK(r.VarS() == -2147483647 - 1);
    CHECK(r.VarS() == 12345);
    CHECK(r.Bool());
    char back[4];
    CHECK(r.Bytes(back, 4) && !memcmp(back, text, 4));
    CHECK(!r.Overflow());
    r.Bits(16);
    CHECK(r.Overflow()); // past the end: zeros, and remembered
    CHECK(r.Bits(8) == 0);

    const uint8_t endless[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    net::BitReader bad(endless, sizeof(endless));
    bad.VarU();
    CHECK(bad.Overflow()); // a VarU longer than 5 steps is rejected
}

net::Schema TestSchema()
{
    net::Schema s;
    net::EntityType car = {"car", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"vx", 16, true}, {"model", 7, false}}};
    net::EntityType ped = {"ped", {{"x", 22, false}, {"y", 22, false}, {"health", 8, false}, {"raw", 32, true}}};
    net::EntityType wide = {"wide", {}}; // more fields than a 32-bit mask
    for (int i = 0; i < 50; i++)
    {
        wide.fields.push_back(net::FieldDesc{"w", (uint8_t)(i % 2 ? 9 : 32), i % 3 == 0});
    }
    s.types.push_back(car);
    s.types.push_back(ped);
    s.types.push_back(wide);
    return s;
}

net::EntityState Entity(uint32_t id, uint8_t type, const net::Schema& s)
{
    net::EntityState e;
    memset(&e, 0, sizeof(e));
    e.id = id;
    e.type = type;
    for (size_t i = 0; i < s.types[type].fields.size(); i++)
    {
        const net::FieldDesc& d = s.types[type].fields[i];
        uint32_t v = Rand();
        if (d.bits < 32)
        {
            v &= (1u << d.bits) - 1;
            if (d.isSigned && (v >> (d.bits - 1)) & 1)
            {
                v |= ~0u << d.bits;
            }
        }
        e.f[i] = (int32_t)v;
    }
    return e;
}

bool Same(const net::Snapshot& a, const net::Snapshot& b)
{
    if (a.entities.size() != b.entities.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.entities.size(); i++)
    {
        if (a.entities[i].id != b.entities[i].id || a.entities[i].type != b.entities[i].type ||
            memcmp(a.entities[i].f, b.entities[i].f, sizeof(a.entities[i].f)))
        {
            return false;
        }
    }
    return true;
}

void RoundTrip(const net::Schema& s, const net::Snapshot& from, const net::Snapshot& to, size_t* pBytes = NULL)
{
    net::BitWriter w;
    net::WriteDelta(w, s, from, to);
    net::BitReader r(w.Data().data(), w.ByteCount());
    net::Snapshot got;
    CHECK(net::ReadDelta(r, s, from, got));
    CHECK(Same(got, to));
    if (pBytes)
    {
        *pBytes = w.ByteCount();
    }
}

void TestDelta()
{
    const net::Schema s = TestSchema();
    net::Snapshot empty, a, b;
    for (uint32_t id = 1; id < 200; id += 1 + Rand() % 3)
    {
        a.entities.push_back(Entity(id, (uint8_t)(Rand() % 3), s));
    }
    RoundTrip(s, empty, a); // everything new
    RoundTrip(s, a, a); // nothing changed
    size_t bytes = 0;
    RoundTrip(s, a, a, &bytes);
    CHECK(bytes <= 2); // an unchanged world costs the entry count only

    // Change a few fields, remove some, add some, and give one id to another type
    b = a;
    b.entities.erase(b.entities.begin() + 3);
    b.entities.erase(b.entities.begin() + 10);
    b.entities[0].f[0] += 5;
    b.entities[5].f[2] ^= 1;
    for (size_t k = 0; k < b.entities.size(); k++)
    {
        if (b.entities[k].type == 2)
        {
            b.entities[k].f[45] ^= 1; // a field in the mask's second word
            break;
        }
    }
    b.entities[7] = Entity(b.entities[7].id, (uint8_t)((b.entities[7].type + 1) % 3), s);
    b.entities.push_back(Entity(500, 0, s));
    b.entities.push_back(Entity(501, 1, s));
    b.entities.push_back(Entity(502, 2, s));
    net::SortById(b);
    RoundTrip(s, a, b);
    RoundTrip(s, b, a);
    RoundTrip(s, b, empty); // everything removed

    // Random worlds against each other
    for (int round = 0; round < 200; round++)
    {
        net::Snapshot x, y;
        for (uint32_t id = 0; id < 300; id++)
        {
            if (Rand() % 3 == 0)
            {
                x.entities.push_back(Entity(id, (uint8_t)(Rand() % 3), s));
            }
            if (Rand() % 3 == 0)
            {
                y.entities.push_back(Entity(id, (uint8_t)(Rand() % 3), s));
            }
        }
        RoundTrip(s, x, y);
    }

    // A truncated delta is rejected, never read past its end
    net::BitWriter w;
    net::WriteDelta(w, s, empty, a);
    for (size_t cut = 0; cut + 1 < w.ByteCount(); cut += 7)
    {
        net::BitReader r(w.Data().data(), cut);
        net::Snapshot got;
        CHECK(!net::ReadDelta(r, s, empty, got));
    }
    // Garbage is rejected or read safely
    for (int round = 0; round < 2000; round++)
    {
        uint8_t junk[64];
        for (size_t i = 0; i < sizeof(junk); i++)
        {
            junk[i] = (uint8_t)Rand();
        }
        net::BitReader r(junk, 1 + Rand() % sizeof(junk));
        net::Snapshot got;
        net::ReadDelta(r, s, a, got);
    }
}

void TestMessages()
{
    net::Welcome w;
    w.slot = 2;
    w.players = 6;
    w.gameType = 3;
    w.fragLimit = 25;
    w.tickMs = 33;
    w.gmp = "wil-multi.gmp";
    w.sty = "wil.sty";
    w.scr = "downtown-6P.scr";
    w.serverTick = 123456;
    net::BitWriter bw;
    net::WriteWelcome(bw, w);
    net::BitReader br(bw.Data().data(), bw.ByteCount());
    CHECK(br.Bits(8) == net::kMsgWelcome);
    net::Welcome got;
    CHECK(net::ReadWelcome(br, got));
    CHECK(got.slot == 2 && got.players == 6 && got.gameType == 3 && got.fragLimit == 25 && got.gmp == w.gmp &&
          got.scr == w.scr && got.serverTick == 123456);

    net::InputCmd cmds[5] = {{100, 1}, {101, 1}, {102, 0x10}, {103, 0x3FFFFF}, {104, 0x3FFFFF}};
    net::BitWriter cw;
    net::WriteCommands(cw, 98, cmds, 5);
    CHECK(cw.ByteCount() <= 16); // 8 + 8 + 4 + 8 + 3 x 23 + 2 bits
    net::BitReader cr(cw.Data().data(), cw.ByteCount());
    CHECK(cr.Bits(8) == net::kMsgCommands);
    uint32_t ack = 0;
    net::InputCmd back[net::kMaxCmdsPerPacket];
    CHECK(net::ReadCommands(cr, &ack, back, net::kMaxCmdsPerPacket) == 5);
    CHECK(ack == 98);
    for (int i = 0; i < 5; i++)
    {
        CHECK(back[i].tick == cmds[i].tick && back[i].buttons == cmds[i].buttons);
    }

    net::SnapshotHeader h = {5000, 4990, 4998}, hb;
    net::BitWriter hw;
    net::WriteSnapshotHeader(hw, h);
    net::BitReader hr(hw.Data().data(), hw.ByteCount());
    CHECK(hr.Bits(8) == net::kMsgSnapshot);
    CHECK(net::ReadSnapshotHeader(hr, hb) && hb.tick == 5000 && hb.baseTick == 4990 && hb.lastCmdTick == 4998);
}
} // namespace

int main()
{
    TestBits();
    printf("bitstream ok\n");
    fflush(stdout);
    TestDelta();
    printf("snapshot deltas ok\n");
    fflush(stdout);
    TestMessages();
    printf("messages ok\n");
    printf("net core tests passed\n");
    return 0;
}
