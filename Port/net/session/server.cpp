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
const int kRing = 64;

float Distance(const net::EntityState& e, float x, float y)
{
    // Every schema type starts with x and y (Fix16 raw: 14 fraction bits)
    const float dx = e.f[0] / 16384.0f - x, dy = e.f[1] / 16384.0f - y;
    return fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
}
} // namespace

std::string net::Scoreboard(const Snapshot& snap)
{
    std::string out;
    for (size_t i = 0; i < snap.entities.size(); i++)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(snap.entities[i], &p, &b))
        {
            char one[64];
            snprintf(one, sizeof(one), " %d:%d/%d(%d)@%.0f,%.0f", p, engine::PlayerFrags(snap.entities[i]), engine::PlayerDeaths(snap.entities[i]),
                     engine::PlayerHealth(snap.entities[i]), snap.entities[i].f[0] / 16384.0f, snap.entities[i].f[1] / 16384.0f);
            out += one;
        }
    }
    return out;
}

bool net::Server::Start(const ServerConfig& config, std::string* error)
{
    mConfig = config;
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
    mTransport->Send(peer, kChannelReliable, w.Data().data(), w.ByteCount(), true);
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
    if (slot < 0 || type != kMsgCommands)
    {
        return; // not joined, or nothing a client sends
    }
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

void net::Server::SendSnapshot(int slot, Slot& s, const Snapshot& world)
{
    // The client's area: every player and the cars they sit in, and the cars, peds and pickups near its own
    float x = 0, y = 0;
    const bool located = engine::PlayerPosition(slot, &x, &y);
    std::vector<const EntityState*> near;
    Snapshot snap;
    snap.tick = mTick;
    std::vector<uint32_t> playersCars;
    for (size_t i = 0; i < world.entities.size(); i++)
    {
        if (world.entities[i].type == engine::kEntityPlayer && engine::PlayerCarId(world.entities[i]))
        {
            playersCars.push_back(engine::PlayerCarId(world.entities[i]));
        }
    }
    for (size_t i = 0; i < world.entities.size(); i++)
    {
        const EntityState& e = world.entities[i];
        const bool playersCar = std::find(playersCars.begin(), playersCars.end(), e.id) != playersCars.end();
        if (e.type == engine::kEntityPlayer || e.type == engine::kEntityWorld || playersCar)
        {
            snap.entities.push_back(e); // wherever they are: a player far off isn't a ped without its car
        }
        else if (located && Distance(e, x, y) <= mConfig.interestRadius)
        {
            near.push_back(&e);
        }
    }
    std::sort(near.begin(), near.end(), [&](const EntityState* a, const EntityState* b) { return Distance(*a, x, y) < Distance(*b, x, y); });
    const Snapshot* base = NULL;
    const Snapshot& acked = s.sent[s.ackTick % kRing];
    if (s.ackTick && acked.tick == s.ackTick && mTick - s.ackTick < (uint32_t)kRing)
    {
        base = &acked;
    }
    static const Snapshot kEmpty;
    const Schema& schema = engine::NetSchema();
    // Fit kMaxPayload by priority, as Quake 3's successors do. The entities the client has stay in every snapshot: one
    // left out would be removed there (and cost a whole new one later). Each gets this tick's state, or, when there is
    // no room, the state the client already has with its age (the ticks since it was new) - a few bits; the client
    // applies only new state and predicts the rest on. Who gets new state: the highest priority, which grows with
    // the age and falls with the distance. New entities come after, nearest first, as room allows.
    auto inBase = [&](const EntityState& e) -> const EntityState* {
        if (!base)
        {
            return NULL;
        }
        auto it = std::lower_bound(base->entities.begin(), base->entities.end(), e.id, [](const EntityState& b, uint32_t id) { return b.id < id; });
        return it != base->entities.end() && it->id == e.id && it->type == e.type ? &*it : NULL;
    };
    auto cost = [&](const EntityState* from, const EntityState& to) -> size_t { // bits, as one entry of a delta
        Snapshot a, b;
        if (from)
        {
            a.entities.push_back(*from);
        }
        b.entities.push_back(to);
        BitWriter t;
        WriteDelta(t, schema, a, b);
        return t.BitCount();
    };
    struct Item
    {
        const EntityState* now;
        const EntityState* had; // in the client's baseline
        EntityState aged; // what it has, with its age
        size_t freshBits, agedBits;
        float priority;
        int pick; // 0: not sent (new, no room), 1: aged, 2: this tick's
    };
    std::vector<Item> items(near.size());
    for (size_t i = 0; i < near.size(); i++)
    {
        Item& it = items[i];
        it.now = near[i];
        it.had = inBase(*near[i]);
        it.freshBits = cost(it.had, *it.now);
        it.pick = 0;
        it.agedBits = 0;
        it.priority = 0;
        if (it.had)
        {
            it.aged = *it.had;
            const int f = engine::AgeField(it.had->type);
            const int age = (int)(mTick - base->tick) + engine::EntityAge(*it.had);
            if (f >= 0)
            {
                it.aged.f[f] = age < 1 ? 1 : age > 63 ? 63 : age;
            }
            it.agedBits = cost(it.had, it.aged);
            it.priority = (float)(age + 1) / (1.0f + Distance(*it.now, x, y) / 6.0f);
        }
        else
        {
            it.priority = 9.0f / (1.0f + Distance(*it.now, x, y) / 6.0f); // new: as if 8 ticks old (one near comes soon)
        }
    }
    size_t fixedBits = 0;
    for (size_t i = 0; i < snap.entities.size(); i++)
    {
        fixedBits += cost(inBase(snap.entities[i]), snap.entities[i]);
    }
    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < items.size(); i++)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return items[a].priority > items[b].priority; });
    BitWriter w;
    const int packets = mConfig.packets < 1 ? 1 : mConfig.packets > kMaxParts ? kMaxParts : mConfig.packets;
    const size_t room = packets == 1 ? kMaxPayload : (size_t)packets * (kMaxPayload - kPartHeader); // bytes
    size_t budget = room * 8 - 96; // the header, the entry count, a margin
    for (;;)
    {
        size_t bits = fixedBits;
        for (size_t i = 0; i < items.size(); i++) // what the client has stays, aged
        {
            items[i].pick = items[i].had ? 1 : 0;
            bits += items[i].had ? items[i].agedBits : 0;
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
        Snapshot candidate = snap;
        size_t fresh = 0;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (items[i].pick == 2)
            {
                candidate.entities.push_back(*items[i].now);
                fresh++;
            }
            else if (items[i].pick == 1)
            {
                candidate.entities.push_back(items[i].aged);
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
            mNearSum += (uint32_t)items.size();
            mKeptSum += (uint32_t)fresh;
            mCut += fresh < items.size();
            s.sent[mTick % kRing] = candidate;
            break;
        }
        budget -= budget / 8; // the estimate was off (ids' gaps): a little less
    }
    static const bool sDeltaStats = getenv("GTA2_NET_DELTASTATS") != NULL;
    if (sDeltaStats && base)
    {
        // What the updates cost now, and with two changes: the changed fields as a list of indices when that is
        // shorter than the mask, and each changed value as the difference from the client's (zigzag, varint by 7 bits)
        static double sNow, sList, sDiff, sBoth;
        static uint32_t sUpdates, sSince;
        const Snapshot& now = s.sent[mTick % kRing];
        const Schema& sc = engine::NetSchema();
        for (size_t i = 0; i < now.entities.size(); i++)
        {
            const EntityState& e = now.entities[i];
            auto it = std::lower_bound(base->entities.begin(), base->entities.end(), e.id, [](const EntityState& b, uint32_t id) { return b.id < id; });
            if (it == base->entities.end() || it->id != e.id || it->type != e.type)
            {
                continue;
            }
            const std::vector<FieldDesc>& fd = sc.types[e.type].fields;
            int changed = 0;
            double full = 0, diff = 0;
            for (size_t f = 0; f < fd.size(); f++)
            {
                const uint32_t mask = fd[f].bits >= 32 ? 0xFFFFFFFFu : (1u << fd[f].bits) - 1;
                if ((uint32_t)(e.f[f] ^ it->f[f]) & mask)
                {
                    changed++;
                    full += fd[f].bits;
                    int64_t d = (int64_t)e.f[f] - (int64_t)it->f[f];
                    uint64_t z = d < 0 ? (uint64_t)(-2 * d - 1) : (uint64_t)(2 * d);
                    int vb = 8;
                    while (z >= 128)
                    {
                        z >>= 7;
                        vb += 8;
                    }
                    diff += vb < fd[f].bits ? vb : fd[f].bits + 1; // a flag bit picks the shorter
                }
            }
            if (!changed)
            {
                continue;
            }
            int idxBits = 1;
            while ((1u << idxBits) < fd.size())
            {
                idxBits++;
            }
            const double maskBits = (double)fd.size(), listBits = 1 + 7 + changed * idxBits;
            const double sel = maskBits < listBits ? maskBits : listBits;
            sNow += maskBits + full;
            sList += sel + full;
            sDiff += maskBits + diff;
            sBoth += sel + diff;
            sUpdates++;
        }
        if (++sSince == 300 * (uint32_t)mSlots.size())
        {
            printf("net: delta updates: %u, bits each now %.0f, with index lists %.0f, with value differences %.0f, both %.0f\n", sUpdates,
                   sUpdates ? sNow / sUpdates : 0, sUpdates ? sList / sUpdates : 0, sUpdates ? sDiff / sUpdates : 0, sUpdates ? sBoth / sUpdates : 0);
            sNow = sList = sDiff = sBoth = 0;
            sUpdates = sSince = 0;
        }
    }
    static const bool sFieldStats = getenv("GTA2_NET_FIELDSTATS") != NULL;
    if (sFieldStats && base)
    {
        // Which fields change from snapshot to snapshot, per entity type (the delta's cost)
        static std::vector<std::vector<uint32_t>> sChanges;
        static uint32_t sSince;
        const Schema& schema = engine::NetSchema();
        sChanges.resize(schema.types.size());
        const Snapshot& now = s.sent[mTick % kRing];
        size_t j = 0;
        for (size_t i = 0; i < now.entities.size(); i++)
        {
            const EntityState& e = now.entities[i];
            while (j < base->entities.size() && base->entities[j].id < e.id)
            {
                j++;
            }
            if (j < base->entities.size() && base->entities[j].id == e.id && base->entities[j].type == e.type)
            {
                std::vector<uint32_t>& c = sChanges[e.type];
                c.resize(schema.types[e.type].fields.size());
                for (size_t f = 0; f < c.size(); f++)
                {
                    c[f] += e.f[f] != base->entities[j].f[f];
                }
            }
        }
        if (++sSince == 300)
        {
            for (size_t t = 0; t < sChanges.size(); t++)
            {
                std::vector<std::pair<uint32_t, size_t>> top;
                for (size_t f = 0; f < sChanges[t].size(); f++)
                {
                    top.push_back(std::make_pair(sChanges[t][f], f));
                }
                std::sort(top.rbegin(), top.rend());
                printf("net: fields changed most (%s):", schema.types[t].name);
                for (size_t k = 0; k < top.size() && k < 14; k++)
                {
                    printf(" %s %u", schema.types[t].fields[top[k].second].name, top[k].first);
                }
                printf("\n");
                sChanges[t].assign(sChanges[t].size(), 0);
            }
            sSince = 0;
        }
    }
    if (w.ByteCount() > kMaxPayload && w.ByteCount() <= room)
    {
        // In parts (each at most kMaxPayload): the client puts them together before reading it
        std::vector<uint8_t> message(w.Data().begin(), w.Data().begin() + w.ByteCount());
        std::vector<std::vector<uint8_t>> parts;
        WriteSnapshotParts(message, mTick, parts);
        for (size_t k = 0; k < parts.size(); k++)
        {
            mTransport->Send(s.peer, kChannelUnreliable, parts[k].data(), parts[k].size(), false);
        }
        mBytesSent += (uint32_t)w.ByteCount();
        mPartsSent += (uint32_t)parts.size();
    }
    else if (w.ByteCount() <= kMaxPayload)
    {
        mTransport->Send(s.peer, kChannelUnreliable, w.Data().data(), w.ByteCount(), false);
        mPartsSent++;
        mBytesSent += (uint32_t)w.ByteCount();
    }
}

void net::Server::Frame()
{
    mTick++; // the tick that just ran
    if (!mStarted && engine::GameRunning())
    {
        // The map script made a ped for every slot: those without a client are out until one joins
        for (size_t i = 0; i < mSlots.size(); i++)
        {
            if (!mSlots[i].used)
            {
                engine::TakeOutOfGame((int)i);
            }
        }
        mStarted = true;
    }
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
    // Tests: GTA2_NET_TESTKILL=<tick>:<victim>:<killer>[,...] kills players through the game's own death
    if (const char* kills = getenv("GTA2_NET_TESTKILL"))
    {
        for (const char* k = kills; k && *k; k = strchr(k, ','), k = k ? k + 1 : NULL)
        {
            unsigned tick = 0;
            int victim = 0, killer = -1;
            if (sscanf(k, "%u:%d:%d", &tick, &victim, &killer) >= 2 && tick == mTick)
            {
                printf("net: test kill of player %d by %d at tick %u\n", victim, killer, mTick);
                engine::DebugKill(victim, killer);
            }
        }
    }
    // Tests: GTA2_NET_TESTARM=<weapon>:<ammo> keeps every player armed (weapon_type, 0 pistol, 1 smg, ...)
    if (const char* arm = getenv("GTA2_NET_TESTARM"))
    {
        int weapon = 0, ammo = 50;
        sscanf(arm, "%d:%d", &weapon, &ammo);
        for (size_t i = 0; i < mSlots.size(); i++)
        {
            engine::DebugArm((int)i, weapon, ammo);
        }
    }
    // Inputs for the next tick: its command, or the last one again if it is late; nobody's slot stands still
    const uint32_t next = mTick + 1;
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        Slot& s = mSlots[i];
        if (s.used && s.cmdTick[next % kRing] == next)
        {
            s.buttons = s.cmdButtons[next % kRing];
        }
        else if (s.used)
        {
            s.late++;
        }
        engine::SetPlayerInput((int)i, s.used ? s.buttons : 0);
    }
    Snapshot world;
    engine::Capture(world);
    world.tick = mTick;
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        if (mSlots[i].used)
        {
            SendSnapshot((int)i, mSlots[i], world);
        }
    }
    mTransport->Flush();
    if (mTick % 300 == 0)
    {
        int used = 0;
        for (size_t i = 0; i < mSlots.size(); i++)
        {
            used += mSlots[i].used;
        }
        uint32_t late = 0;
        for (size_t i = 0; i < mSlots.size(); i++)
        {
            late += mSlots[i].late;
            mSlots[i].late = 0;
        }
        int sent = 0, driven = 0, same = 0;
        engine::DriverStats(world, &sent, &driven, &same);
        printf("net: tick %u, %d players, %zu entities (%d drivers), %.0f bytes per snapshot, %u late commands; frags/deaths(health)%s\n",
               mTick, used, world.entities.size(), sent, used ? mBytesSent / 300.0 / used : 0.0, late, Scoreboard(world).c_str());
        mBytesSent = 0;
        printf("net: tick %u, %.2f packets per snapshot\n", mTick, used ? mPartsSent / 300.0 / used : 0.0);
        mPartsSent = 0;
        for (size_t i = 0; i < mSlots.size(); i++)
        {
            if (mSlots[i].used)
            {
                printf("net: tick %u, player %zu sees %.1f blocks (centre to corner)\n", mTick, i, engine::PlayerViewRadius((int)i));
            }
        }
        printf("net: tick %u, snapshots had new state for %.0f%% of the entities in the clients' areas (%u of %u not for all)\n", mTick,
               mNearSum ? 100.0 * mKeptSum / mNearSum : 100.0, mCut, used * 300);
        mNearSum = mKeptSum = mCut = 0;
    }
}
