// Port/net (docs/netcode.md, "Server"): the authoritative game. Every frame, after the tick: read the clients, set
// every player's input for the next tick, and send each client a snapshot of its area as a delta to the newest one
// it has acknowledged.
#include "server.hpp"
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

// A client's Escape (the key's code in the input: Source/input_bits.hpp) is its own (Client::ChooseInput: its leave
// prompt): here it would open the server's quit prompt for that player, whose yes ended the server's game
uint32_t WithoutEscape(uint32_t buttons)
{
    const uint32_t kKeyDown = 0x200000, kKeyBits = 0x1FF000, kEscape = 0x01; // DIK_ESCAPE
    return ((buttons & kKeyBits) >> 12) == kEscape ? buttons & ~(kKeyDown | kKeyBits) : buttons;
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
    std::vector<std::string> mapNames;
    for (const MapFiles& m : mConfig.voteMaps)
    {
        mapNames.push_back(m.name);
    }
    mMap = (size_t)(mConfig.currentMap >= 0 ? mConfig.currentMap : 0);
    mRounds.Start(mConfig.rules, mapNames, mConfig.currentMap);
    engine::CurrentRound() = mRounds.State();
    mProbe.Start();
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
        // The slot this name had (a client back after its connection dropped: the same player), else the first free
        int slot = 0;
        while (slot < (int)mSlots.size() && (mSlots[slot].used || mSlots[slot].name != hello.name))
        {
            slot++;
        }
        for (int i = 0; slot == (int)mSlots.size() && i < (int)mSlots.size(); i++)
        {
            slot = mSlots[i].used ? slot : i;
        }
        auto ban = mBanned.find(hello.name);
        if (ban != mBanned.end() && mTick < ban->second)
        {
            Reject reject;
            reject.reason = "kicked by vote, for a while";
            WriteReject(w, reject);
        }
        else if (slot == (int)mSlots.size())
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
            welcome.maps = mRounds.Maps();
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
            s.cmdButtons[cmds[i].tick % kRing] = WithoutEscape(cmds[i].buttons);
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
    if (type == kMsgPing) // polled in a frame, not between them (Idle): late by that much
    {
        std::vector<uint8_t> pong = e.data;
        pong[0] = kMsgPong;
        mTransport->Send(e.peer, kChannelUnreliable, pong.data(), pong.size(), false);
        return;
    }
    const int slot = SlotOfPeer(e.peer);
    if (slot >= 0 && type == kMsgCommands) // else: not joined, or nothing a client sends
    {
        OnCommands(slot, r);
    }
    else if (slot >= 0 && type == kMsgCommand)
    {
        OnCommand(slot, r);
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
    if (mGeneration > 0)
    {
        engine::NewRound(); // a new map: no scores, weapons or power-ups from the last
        Carry(mRounds.NewGame(mTick, (int)mMap, RoundPlayers(Snapshot())));
    }
    mStarted = true;
}

void net::Server::PollTransport()
{
    for (const TransportEvent& e : mPending)
    {
        OnEvent(e);
    }
    mPending.clear();
    TransportEvent e;
    while (mTransport->Poll(e))
    {
        OnEvent(e);
    }
}

// Between frames: a client's ping goes back at once (its round trip is then the network's alone, not our tick's
// wait); anything else waits for the frame, as a join or a leave changes the game
void net::Server::Idle()
{
    TransportEvent e;
    while (mTransport && mTransport->Poll(e))
    {
        if (e.type == TransportEvent::kReceive && e.channel == kChannelUnreliable && !e.data.empty() && e.data[0] == kMsgPing)
        {
            e.data[0] = kMsgPong;
            mTransport->Send(e.peer, kChannelUnreliable, e.data.data(), e.data.size(), false);
            mTransport->Flush();
        }
        else
        {
            mPending.push_back(e);
        }
    }
}

void net::Server::OnEvent(const TransportEvent& e)
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
            const std::string name = mSlots[slot].name;
            mSlots[slot] = Slot();
            mSlots[slot].name = name; // kept for its return
            engine::TakeOutOfGame(slot);
            mRounds.Left(slot);
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
            s.buttons &= 0xFFF; // the last one again, its controls only: a key event (chat) happens once
        }
        engine::SetPlayerInput((int)i, s.used ? s.buttons : 0);
    }
}

// GTA2_NET_JERKLOG: the same measure as the clients', on the server's own state
// The round flow's view of the slots: who is in, a person or an arena bot, and the score that counts (frags, or
// points in a points game)
std::vector<net::RoundPlayer> net::Server::RoundPlayers(const Snapshot& world) const
{
    std::vector<RoundPlayer> players(mSlots.size());
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        players[i].in = mSlots[i].used && engine::SlotInGame((int)i);
        players[i].voter = mSlots[i].used && mSlots[i].peer != kArenaPeer;
        players[i].alwaysReady = mSlots[i].peer == kArenaPeer;
        players[i].name = mSlots[i].name;
    }
    for (const EntityState& e : world.entities)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(e, &p, &b) && p < (int)players.size())
        {
            players[p].score = mConfig.game.gameType == 2 ? engine::PlayerPoints(e) : engine::PlayerFrags(e);
        }
    }
    return players;
}

// The round flow's tick (session/round_flow.cpp), its state for the snapshots, and what it asks for
void net::Server::RunRounds(const Snapshot& world)
{
    mRoundPlayers = RoundPlayers(world);
    if (!mStarted || engine::MapChangePending())
    {
        engine::CurrentRound() = mRounds.State();
        return;
    }
    const int before = mRounds.State().number;
    const RoundActions actions = mRounds.Tick(mTick, mRoundPlayers);
    if (mRounds.State().number != before)
    {
        printf("net: round %d's scores:%s\n", before, Scoreboard(world).c_str());
    }
    Carry(actions);
    engine::CurrentRound() = mRounds.State();
}

void net::Server::Carry(const RoundActions& actions)
{
    for (const auto& n : actions.notices)
    {
        Notice(n.first, n.second);
    }
    if (actions.newRound)
    {
        engine::NewRound();
    }
    if (actions.kick >= 0)
    {
        Kick(actions.kick);
    }
    if (actions.changeMap >= 0)
    {
        ChangeToMap(actions.changeMap);
    }
}

void net::Server::OnCommand(int slot, BitReader& r)
{
    std::string text;
    if (!ReadText(r, text) || !mStarted)
    {
        return;
    }
    printf("net: player %d (%s): %s\n", slot, mSlots[slot].name.c_str(), text.c_str());
    RoundActions actions;
    mRounds.Command(mTick, slot, text, mRoundPlayers, actions);
    Carry(actions);
    engine::CurrentRound() = mRounds.State();
}

void net::Server::Notice(int slot, const std::string& text)
{
    BitWriter w;
    WriteText(w, kMsgNotice, text);
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        if (mSlots[i].used && mSlots[i].peer != kArenaPeer && (slot < 0 || slot == (int)i))
        {
            mTransport->Send(mSlots[i].peer, kChannelReliable, w.Data().data(), w.ByteCount(), true);
        }
    }
    printf("net: notice%s: %s\n", slot < 0 ? "" : (" to player " + std::to_string(slot)).c_str(), text.c_str());
}

// Out by vote: told why, disconnected, and its name turned away for 10 minutes
void net::Server::Kick(int slot)
{
    Slot& s = mSlots[slot];
    if (!s.used || s.peer == kArenaPeer)
    {
        return;
    }
    const uint32_t tps = 1000 / (mConfig.game.tickMs > 0 ? mConfig.game.tickMs : 33);
    mBanned[s.name] = mTick + 600 * tps;
    BitWriter w;
    Reject reject;
    reject.reason = "kicked by vote";
    WriteReject(w, reject);
    mTransport->Send(s.peer, kChannelReliable, w.Data().data(), w.ByteCount(), true);
    mTransport->Flush();
    mTransport->Disconnect(s.peer);
    printf("net: player %d (%s) kicked\n", slot, s.name.c_str());
    mRounds.Left(slot);
    s = Slot(); // the name isn't kept for a return
    engine::TakeOutOfGame(slot);
}

// A map for everyone: after a pause (the round's winner on the HUD), the game ends here and on the clients and the
// next starts at once on it
void net::Server::ChangeToMap(int map)
{
    mMap = (size_t)map;
    const MapFiles& m = mConfig.voteMaps[mMap];
    mConfig.game.gmp = m.gmp; // for the welcome of those joining
    mConfig.game.sty = m.sty;
    mConfig.game.scr = m.scr;
    printf("net: the next map: %s\n", m.name.c_str());
    MapChange change;
    change.gmp = m.gmp;
    change.sty = m.sty;
    change.scr = m.scr;
    change.game = (uint16_t)(engine::GameGeneration() + 1);
    BitWriter w;
    WriteMapChange(w, change);
    for (const Slot& s : mSlots)
    {
        if (s.used && s.peer != kArenaPeer)
        {
            mTransport->Send(s.peer, kChannelReliable, w.Data().data(), w.ByteCount(), true);
        }
    }
    engine::ChangeMap(m.gmp, m.sty, m.scr, 90);
}

void net::Server::StartOver()
{
    mGeneration = engine::GameGeneration();
    mStarted = false;
    for (Slot& s : mSlots)
    {
        s.ackTick = 0; // the old game's snapshots are no baseline: full ones again
        for (Snapshot& sent : s.sent)
        {
            sent = Snapshot();
        }
    }
    mArena.NewMap();
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
    printf("net: tick %u, %.2f packets per snapshot; %s\n", mTick, used ? mStats.parts / 300.0 / used : 0.0, engine::SpawnStats().c_str());
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        if (mSlots[i].used)
        {
            printf("net: tick %u, player %zu sees %.1f blocks (centre to corner), spawned for %d ticks ahead\n", mTick, i, engine::PlayerViewRadius((int)i),
                   engine::SpawnLead((int)i));
        }
    }
    printf("net: tick %u, snapshots had new state for %.0f%% of the entities in the clients' areas (%u of %u not for all)%s\n", mTick,
           mStats.inArea ? 100.0 * mStats.fresh / mStats.inArea : 100.0, mStats.cut, used * 300, mStats.unsent ? "; some didn't fit at all" : "");
    mStats = Stats();
}

void net::Server::Frame()
{
    mTick++; // the tick that just ran
    engine::KeepMapChange();
    if (engine::GameGeneration() != mGeneration)
    {
        StartOver();
    }
    engine::GuardPeds();
    StartGameOnce();
    PollTransport();
    mProbe.Tests(mTick, (int)mSlots.size());
    Snapshot world;
    engine::Capture(world);
    world.tick = mTick;
    mProbe.Captured(mTick, world);
    SetNextInputs(world);
    RunRounds(world);
    if (mConfig.endTick && mTick >= mConfig.endTick)
    {
        mArena.Finish(world);
        fflush(stdout);
        _Exit(0); // No game shutdown; the trainer only reads the result line
    }
    int players = 0;
    for (size_t i = 0; i < mSlots.size(); i++)
    {
        const Slot& s = mSlots[i];
        players += s.used;
        // A client shows its player ahead of here by how early its commands come and the way back of the snapshots
        // (half the round trip, from what it acknowledged); the next tick spawns for there
        const bool remote = s.used && s.peer != kArenaPeer && s.ackTick && s.lastCmdTick;
        engine::SetSpawnLead((int)i, remote ? (int)(s.lastCmdTick - mTick) + (int)(mTick - s.ackTick) / 2 : 0);
    }
    engine::SetSpawnPlayers(players);
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
