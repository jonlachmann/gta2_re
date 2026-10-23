// Port/net (docs/netcode.md, "Server"): the authoritative game. Every frame, after the tick: read the clients, set
// every player's input for the next tick, and send each client a snapshot of its area as a delta to the newest one
// it has acknowledged.
#include "session.hpp"
#include <algorithm>
#include <math.h>
#include <stdio.h>

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
    // The client's area: every player, and the cars and peds near its own
    float x = 0, y = 0;
    const bool located = engine::PlayerPosition(slot, &x, &y);
    std::vector<const EntityState*> near;
    Snapshot snap;
    snap.tick = mTick;
    for (size_t i = 0; i < world.entities.size(); i++)
    {
        const EntityState& e = world.entities[i];
        if (e.type == engine::kEntityPlayer)
        {
            snap.entities.push_back(e);
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
    // Fit kMaxPayload: drop the farthest entities until it does (ponytail: per-entity baselines and priorities, N3)
    size_t keep = near.size();
    BitWriter w;
    for (;;)
    {
        Snapshot candidate = snap;
        for (size_t i = 0; i < keep; i++)
        {
            candidate.entities.push_back(*near[i]);
        }
        SortById(candidate);
        w = BitWriter();
        SnapshotHeader h = {mTick, base ? base->tick : mTick, s.lastCmdTick};
        WriteSnapshotHeader(w, h);
        WriteDelta(w, engine::NetSchema(), base ? *base : kEmpty, candidate);
        if (w.ByteCount() <= kMaxPayload || keep == 0)
        {
            s.sent[mTick % kRing] = candidate;
            break;
        }
        keep = keep * 3 / 4;
    }
    if (w.ByteCount() <= kMaxPayload)
    {
        mTransport->Send(s.peer, kChannelUnreliable, w.Data().data(), w.ByteCount(), false);
        mBytesSent += (uint32_t)w.ByteCount();
    }
}

void net::Server::Frame()
{
    mTick++; // the tick that just ran
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
            }
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
        printf("net: tick %u, %d players, %zu entities, %.0f bytes per snapshot, %u late commands\n", mTick, used,
               world.entities.size(), used ? mBytesSent / 300.0 / used : 0.0, late);
        mBytesSent = 0;
    }
}
