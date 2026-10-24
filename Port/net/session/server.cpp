// Port/net (docs/netcode.md, "Server"): the authoritative game. Every frame, after the tick: read the clients, set
// every player's input for the next tick, and send each client a snapshot of its area as a delta to the newest one
// it has acknowledged.
#include "session.hpp"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
const uint32_t kArenaPeer = ~0u; // an arena bot's slot (no transport peer has it)

float Distance(const net::EntityState& e, float x, float y)
{
    // Every schema type with a place starts with x and y
    const float dx = net::engine::Blocks(e.f[0]) - x, dy = net::engine::Blocks(e.f[1]) - y;
    return fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
}

// Its state in the client's baseline (NULL: new to it)
const net::EntityState* InBase(const net::Snapshot* base, const net::EntityState& e)
{
    if (!base)
    {
        return NULL;
    }
    auto it = std::lower_bound(base->entities.begin(), base->entities.end(), e.id, [](const net::EntityState& b, uint32_t id) { return b.id < id; });
    return it != base->entities.end() && it->id == e.id && it->type == e.type ? &*it : NULL;
}

// Tests: GTA2_NET_TESTKILL=<tick>:<victim>:<killer>[,...] kills players through the game's own death
struct TestKill
{
    unsigned tick;
    int victim, killer;
};
std::vector<TestKill> TestKills()
{
    std::vector<TestKill> out;
    for (const char* k = net::Env().testKill; k && *k; k = strchr(k, ','), k = k ? k + 1 : NULL)
    {
        TestKill t = {0, 0, -1};
        if (sscanf(k, "%u:%d:%d", &t.tick, &t.victim, &t.killer) >= 2)
        {
            out.push_back(t);
        }
    }
    return out;
}
} // namespace

std::string net::Scoreboard(const Snapshot& snap)
{
    std::string out;
    for (const EntityState& e : snap.entities)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(e, &p, &b))
        {
            char one[64];
            snprintf(one, sizeof(one), " %d:%d/%d(%d)@%.0f,%.0f", p, engine::PlayerFrags(e), engine::PlayerDeaths(e), engine::PlayerHealth(e),
                     engine::Blocks(e.f[0]), engine::Blocks(e.f[1]));
            out += one;
        }
    }
    return out;
}

bool net::Server::Start(const ServerConfig& config, std::string* error)
{
    mConfig = config;
    mConfig.packets = config.packets < 1 ? 1 : config.packets > kMaxParts ? kMaxParts : config.packets;
    mSlots.assign(config.game.players, Slot());
    mTransport.reset(CreateServerTransport(config.port, (uint32_t)config.game.players + 4, error));
    if (!mTransport)
    {
        return false;
    }
    if (config.lagMs || config.jitterMs || config.lossPercent)
    {
        mTransport.reset(CreateConditioner(mTransport.release(), config.lagMs, config.jitterMs, config.lossPercent));
    }
    if (config.arena.size() > mSlots.size())
    {
        *error = "more arena bots than player slots";
        return false;
    }
    if (!mArena.Start(config.arena, error))
    {
        return false;
    }
    for (int i = 0; i < mArena.Count(); i++)
    {
        mSlots[i].used = true; // no peer: the arena's
        mSlots[i].peer = kArenaPeer;
        mSlots[i].name = "bot" + std::to_string(i);
    }
    mJerk.PrintJumps(Env().jerkLog >= 2);
    printf("net: server on port %u, %d player slots, %s\n", config.port, config.game.players, config.game.scr.c_str());
    return true;
}

int net::Server::SlotOfPeer(uint32_t peer) const
{
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        if (mSlots[i].used && mSlots[i].peer == peer)
        {
            return (int)i;
        }
    }
    return -1;
}

void net::Server::OnHello(uint32_t peer, BitReader& r)
{
    Hello hello;
    BitWriter w;
    if (!ReadHello(r, hello) || hello.version != kProtocolVersion)
    {
        Reject reject;
        reject.reason = "protocol version differs";
        WriteReject(w, reject);
    }
    else if (SlotOfPeer(peer) < 0)
    {
        int slot = 0;
        while (slot < (int)mSlots.size() && mSlots[slot].used)
        {
            slot++;
        }
        if (slot == (int)mSlots.size())
        {
            Reject reject;
            reject.reason = "the server is full";
            WriteReject(w, reject);
        }
        else
        {
            Slot& s = mSlots[slot];
            s = Slot();
            s.used = true;
            s.peer = peer;
            s.name = hello.name;
            Welcome welcome;
            welcome.slot = (uint8_t)slot;
            welcome.players = (uint8_t)mSlots.size();
            welcome.gameType = (uint8_t)mConfig.game.gameType;
            welcome.fragLimit = (uint16_t)mConfig.game.fragLimit;
            welcome.timeLimitMin = (uint16_t)mConfig.game.timeLimitMin;
            welcome.tickMs = (uint16_t)mConfig.game.tickMs;
            welcome.police = (uint8_t)mConfig.game.police;
            welcome.gmp = mConfig.game.gmp;
            welcome.sty = mConfig.game.sty;
            welcome.scr = mConfig.game.scr;
            welcome.serverTick = mTick;
            WriteWelcome(w, welcome);
            engine::PutInGame(slot, true); // a fresh start at the next restart zone
            engine::SetPlayerName(slot, hello.name.c_str());
            printf("net: %s joined as player %d\n", hello.name.c_str(), slot);
        }
    }
    if (w.ByteCount()) // a second hello from a client already in: nothing to say
    {
        mTransport->Send(peer, kChannelReliable, w.Data().data(), w.ByteCount(), true);
    }
}

void net::Server::OnCommands(int slot, BitReader& r)
{
    Slot& s = mSlots[slot];
    uint32_t ack = 0;
    InputCmd cmds[kMaxCmdsPerPacket];
    const int n = ReadCommands(r, &ack, cmds, kMaxCmdsPerPacket);
    if (n < 0)
    {
        return;
    }
    if (ack > s.ackTick && ack <= mTick)
    {
        s.ackTick = ack;
    }
    for (int i = 0; i < n; i++)
    {
        // The newest tick a command arrived for, late or not: the client steers its lead by it
        if (cmds[i].tick <= mTick + kRing)
        {
            s.lastCmdTick = cmds[i].tick > s.lastCmdTick ? cmds[i].tick : s.lastCmdTick;
        }
        // Commands for ticks still to come (and not too far ahead) go into the buffer
        if (cmds[i].tick > mTick && cmds[i].tick <= mTick + kRing)
        {
            s.cmdTick[cmds[i].tick % kRing] = cmds[i].tick;
            s.cmdButtons[cmds[i].tick % kRing] = cmds[i].buttons;
        }
    }
}

void net::Server::OnReceive(const TransportEvent& e)
{
    BitReader r(e.data.data(), e.data.size());
    const uint32_t type = r.Bits(8);
    if (type == kMsgHello)
    {
        OnHello(e.peer, r);
        return;
    }
    const int slot = SlotOfPeer(e.peer);
    if (slot >= 0 && type == kMsgCommands) // else: not joined, or nothing a client sends
    {
        OnCommands(slot, r);
    }
}

// Each nearby entity with what it costs new and aged, and its priority: the age plus one, over one plus a sixth of the
// distance (an entity new to the client counts as 8 ticks old: one near comes soon)
std::vector<net::Server::Item> net::Server::PriceArea(const std::vector<const EntityState*>& area, const Snapshot* base, float x, float y) const
{
    const Schema& schema = engine::NetSchema();
    std::vector<Item> items(area.size());
    for (size_t i = 0; i < area.size(); i++)
    {
        Item& it = items[i];
        it.now = area[i];
        it.had = InBase(base, *area[i]);
        it.freshBits = EntryBits(schema, it.had, *it.now);
        it.pick = 0;
        it.agedBits = 0;
        if (it.had)
        {
            it.aged = *it.had;
            const int f = engine::AgeField(it.had->type);
            const int age = (int)(mTick - base->tick) + engine::EntityAge(*it.had);
            if (f >= 0)
            {
                it.aged.f[f] = age < 1 ? 1 : age > 63 ? 63 : age;
            }
            it.agedBits = EntryBits(schema, it.had, it.aged);
            it.priority = (float)(age + 1) / (1.0f + Distance(*it.now, x, y) / 6.0f);
        }
        else
        {
            it.priority = 9.0f / (1.0f + Distance(*it.now, x, y) / 6.0f);
        }
    }
    return items;
}

// Fit the snapshot into its packets by priority, as Quake 3's successors do. The entities the client has stay in every
// snapshot: one left out would be removed there (and cost a whole new one later). Each gets this tick's state, or, when
// there is no room, the state the client already has with its age (the ticks since it was new) - a few bits; the
// client applies only new state and predicts the rest on. Who gets new state: the highest priority. New entities come
// after, as room allows. False: it fits no budget (nothing to send)
bool net::Server::Fill(Slot& s, const Snapshot& fixed, std::vector<Item>& items, const Snapshot* base, BitWriter& w)
{
    static const Snapshot kEmpty;
    const Schema& schema = engine::NetSchema();
    size_t fixedBits = 0;
    for (const EntityState& e : fixed.entities)
    {
        fixedBits += EntryBits(schema, InBase(base, e), e);
    }
    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < items.size(); i++)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return items[a].priority > items[b].priority; });
    const size_t room = mConfig.packets == 1 ? kMaxPayload : (size_t)mConfig.packets * (kMaxPayload - kPartHeader); // bytes
    size_t budget = room * 8 - 96; // the header, the entry count, a margin
    for (;;)
    {
        size_t bits = fixedBits;
        for (Item& it : items) // what the client has stays, aged
        {
            it.pick = it.had ? 1 : 0;
            bits += it.had ? it.agedBits : 0;
        }
        for (size_t k = 0; k < order.size(); k++) // then new state, by priority, as room allows
        {
            Item& it = items[order[k]];
            const size_t was = it.had ? it.agedBits : 0;
            if (bits - was + it.freshBits <= budget)
            {
                bits += it.freshBits - was;
                it.pick = 2;
            }
        }
        Snapshot candidate = fixed;
        size_t fresh = 0;
        for (const Item& it : items)
        {
            if (it.pick == 2)
            {
                candidate.entities.push_back(*it.now);
                fresh++;
            }
            else if (it.pick == 1)
            {
                candidate.entities.push_back(it.aged);
            }
        }
        engine::SetSnapshotReach(candidate, mConfig.interestRadius); // nothing it has goes missing
        SortById(candidate);
        w = BitWriter();
        SnapshotHeader h = {mTick, base ? base->tick : mTick, s.lastCmdTick};
        WriteSnapshotHeader(w, h);
        WriteDelta(w, schema, base ? *base : kEmpty, candidate);
        if (w.ByteCount() <= room || budget < 2048)
        {
            mStats.inArea += (uint32_t)items.size();
            mStats.fresh += (uint32_t)fresh;
            mStats.cut += fresh < items.size();
            s.sent[mTick % kRing] = candidate;
            return w.ByteCount() <= room;
        }
        budget -= budget / 8; // the estimate was off (ids' gaps): a little less
    }
}

// One packet, or parts of at most kMaxPayload each: the client puts them together before reading it
void net::Server::Transmit(const Slot& s, const BitWriter& w)
{
    if (w.ByteCount() > kMaxPayload)
    {
        std::vector<uint8_t> message(w.Data().begin(), w.Data().begin() + w.ByteCount());
        std::vector<std::vector<uint8_t>> parts;
        WriteSnapshotParts(message, mTick, parts);
        for (size_t k = 0; k < parts.size(); k++)
        {
            mTransport->Send(s.peer, kChannelUnreliable, parts[k].data(), parts[k].size(), false);
        }
        mStats.parts += (uint32_t)parts.size();
    }
    else
    {
        mTransport->Send(s.peer, kChannelUnreliable, w.Data().data(), w.ByteCount(), false);
        mStats.parts++;
    }
    mStats.bytes += (uint32_t)w.ByteCount();
}

// The client's area: every player and the cars they sit in (wherever they are: a player far off isn't a ped without its
// car), and the cars, peds and pickups near its own, nearest first
void net::Server::SendSnapshot(int slot, Slot& s, const Snapshot& world)
{
    float x = 0, y = 0;
    const bool located = engine::PlayerPosition(slot, &x, &y);
    std::vector<uint32_t> playersCars;
    for (const EntityState& e : world.entities)
    {
        if (engine::PlayerCarId(e))
        {
            playersCars.push_back(engine::PlayerCarId(e));
        }
    }
    Snapshot fixed;
    fixed.tick = mTick;
    std::vector<const EntityState*> area;
    for (const EntityState& e : world.entities)
    {
        const bool playersCar = std::find(playersCars.begin(), playersCars.end(), e.id) != playersCars.end();
        if (e.type == engine::kEntityPlayer || e.type == engine::kEntityWorld || playersCar)
        {
            fixed.entities.push_back(e);
        }
        else if (located && Distance(e, x, y) <= mConfig.interestRadius)
        {
            area.push_back(&e);
        }
    }
    std::sort(area.begin(), area.end(), [&](const EntityState* a, const EntityState* b) { return Distance(*a, x, y) < Distance(*b, x, y); });
    const Snapshot& acked = s.sent[s.ackTick % kRing];
    const Snapshot* base = s.ackTick && acked.tick == s.ackTick && mTick - s.ackTick < (uint32_t)kRing ? &acked : NULL;
    std::vector<Item> items = PriceArea(area, base, x, y);
    BitWriter w;
    if (Fill(s, fixed, items, base, w))
    {
        Transmit(s, w);
    }
    else
    {
        mStats.unsent++;
    }
}

void net::Server::StartGameOnce()
{
    if (mStarted || !engine::GameRunning())
    {
        return;
    }
    // The map script made a ped for every slot: those without a client are out until one joins
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        if (!mSlots[i].used)
        {
            engine::TakeOutOfGame((int)i);
        }
        else if (mSlots[i].peer == kArenaPeer)
        {
            engine::PutInGame((int)i, true);
            engine::SetPlayerName((int)i, mSlots[i].name.c_str());
        }
    }
    if (mConfig.seed)
    {
        engine::SeedRandom(mConfig.seed);
    }
    mStarted = true;
}

void net::Server::PollTransport()
{
    TransportEvent e;
    while (mTransport->Poll(e))
    {
        if (e.type == TransportEvent::kReceive)
        {
            OnReceive(e);
        }
        else if (e.type == TransportEvent::kDisconnect)
        {
            const int slot = SlotOfPeer(e.peer);
            if (slot >= 0)
            {
                printf("net: player %d (%s) left\n", slot, mSlots[slot].name.c_str());
                mSlots[slot] = Slot();
                engine::TakeOutOfGame(slot);
            }
        }
    }
}

void net::Server::RunTestHooks()
{
    static const std::vector<TestKill> sKills = TestKills();
    for (const TestKill& k : sKills)
    {
        if (k.tick == mTick)
        {
            printf("net: test kill of player %d by %d at tick %u\n", k.victim, k.killer, mTick);
            engine::DebugKill(k.victim, k.killer);
        }
    }
    // GTA2_NET_TESTPOWERUP=<tick>:<player>:<power-up>[,...] gives players power-ups (12: an instant gang)
    for (const char* k = Env().testPowerUp; k && *k; k = strchr(k, ','), k = k ? k + 1 : NULL)
    {
        unsigned tick = 0;
        int player = 0, powerUp = 0;
        if (sscanf(k, "%u:%d:%d", &tick, &player, &powerUp) == 3 && tick == mTick)
        {
            printf("net: test power-up %d for player %d at tick %u\n", powerUp, player, mTick);
            engine::DebugPowerUp(player, powerUp);
        }
    }
    // GTA2_NET_TESTARM=<weapon>:<ammo> keeps every player armed (weapon_type, 0 pistol, 1 smg, ...)
    if (const char* arm = Env().testArm)
    {
        int weapon = 0, ammo = 50;
        sscanf(arm, "%d:%d", &weapon, &ammo);
        for (size_t i = 0; i < mSlots.size(); i++)
        {
            engine::DebugArm((int)i, weapon, ammo);
        }
    }
}

// Inputs for the next tick: its command, or the last one again if it is late, or the arena bot's; nobody's slot
// stands still
void net::Server::SetNextInputs(const Snapshot& world)
{
    const uint32_t next = mTick + 1;
    if (mArena.Count() && mStarted)
    {
        mArena.Think(world, mTick);
    }
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        Slot& s = mSlots[i];
        if (s.peer == kArenaPeer)
        {
            s.buttons = mStarted ? mArena.Buttons((int)i) : 0;
        }
        else if (s.used && s.cmdTick[next % kRing] == next)
        {
            s.buttons = s.cmdButtons[next % kRing];
        }
        else if (s.used)
        {
            s.late++;
        }
        engine::SetPlayerInput((int)i, s.used ? s.buttons : 0);
    }
}

// GTA2_NET_JERKLOG: the same measure as the clients', on the server's own state
void net::Server::FeedJerk(const Snapshot& world)
{
    if (!Env().jerkLog)
    {
        return;
    }
    std::vector<engine::Pose> poses;
    for (const EntityState& e : world.entities)
    {
        engine::Pose p;
        if (engine::PoseOf(e, &p))
        {
            poses.push_back(p);
        }
    }
    mJerk.Feed(mTick, poses);
    if (mTick % 900 == 0)
    {
        mJerk.Report("server", mTick);
    }
}

void net::Server::PrintStats(const Snapshot& world)
{
    int used = 0;
    uint32_t late = 0;
    for (Slot& s : mSlots)
    {
        used += s.used;
        late += s.late;
        s.late = 0;
    }
    int sent = 0, driven = 0, same = 0;
    engine::DriverStats(world, &sent, &driven, &same);
    printf("net: tick %u, %d players, %zu entities (%d drivers), %.0f bytes per snapshot, %u late commands; frags/deaths(health)%s\n", mTick, used,
           world.entities.size(), sent, used ? mStats.bytes / 300.0 / used : 0.0, late, Scoreboard(world).c_str());
    printf("net: tick %u, %.2f packets per snapshot\n", mTick, used ? mStats.parts / 300.0 / used : 0.0);
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        if (mSlots[i].used)
        {
            printf("net: tick %u, player %zu sees %.1f blocks (centre to corner)\n", mTick, i, engine::PlayerViewRadius((int)i));
        }
    }
    printf("net: tick %u, snapshots had new state for %.0f%% of the entities in the clients' areas (%u of %u not for all)%s\n", mTick,
           mStats.inArea ? 100.0 * mStats.fresh / mStats.inArea : 100.0, mStats.cut, used * 300, mStats.unsent ? "; some didn't fit at all" : "");
    mStats = Stats();
}

void net::Server::Frame()
{
    mTick++; // the tick that just ran
    engine::GuardPeds();
    StartGameOnce();
    PollTransport();
    RunTestHooks();
    Snapshot world;
    engine::Capture(world);
    world.tick = mTick;
    SetNextInputs(world);
    FeedJerk(world);
    if (mConfig.endTick && mTick >= mConfig.endTick)
    {
        mArena.Finish(world);
        fflush(stdout);
        _Exit(0); // ponytail: no game shutdown; the trainer only reads the result line
    }
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        if (mSlots[i].used && mSlots[i].peer != kArenaPeer)
        {
            SendSnapshot((int)i, mSlots[i], world);
        }
    }
    mTransport->Flush();
    if (mTick % 300 == 0)
    {
        PrintStats(world);
    }
}
