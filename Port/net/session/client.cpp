// Port/net (docs/netcode.md, "Client"): joins a server, starts the same network game, sends its input every frame
// (repeating the last few commands), and puts the server's snapshots into its game.
#include "session.hpp"
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <thread>

namespace
{
const int kRing = 64;

// Polls until a message of <want> arrives on the reliable channel, a disconnect, or <ms> pass
bool WaitFor(net::Transport* t, net::TransportEvent::Type want, int ms, net::TransportEvent* pOut)
{
    for (int i = 0; i < ms; i++)
    {
        while (t->Poll(*pOut))
        {
            if (pOut->type == want || pOut->type == net::TransportEvent::kDisconnect)
            {
                return pOut->type == want;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

bool net::Client::Connect(const ClientConfig& config, engine::NetworkGame* pGame, std::string* error)
{
    mConfig = config;
    mTransport.reset(CreateClientTransport(config.host.c_str(), config.port, error));
    if (!mTransport)
    {
        return false;
    }
    TransportEvent e;
    if (!WaitFor(mTransport.get(), TransportEvent::kConnect, 6000, &e))
    {
        *error = "no answer from " + config.host;
        return false;
    }
    Hello hello;
    hello.name = config.name;
    BitWriter w;
    WriteHello(w, hello);
    mTransport->Send(0, kChannelReliable, w.Data().data(), w.ByteCount(), true);
    mTransport->Flush();
    if (!WaitFor(mTransport.get(), TransportEvent::kReceive, 6000, &e))
    {
        *error = "the server didn't welcome us";
        return false;
    }
    BitReader r(e.data.data(), e.data.size());
    const uint32_t type = r.Bits(8);
    if (type == kMsgReject)
    {
        Reject reject;
        ReadReject(r, reject);
        *error = "rejected: " + reject.reason;
        return false;
    }
    Welcome welcome;
    if (type != kMsgWelcome || !ReadWelcome(r, welcome) || welcome.version != kProtocolVersion)
    {
        *error = "a welcome we don't understand";
        return false;
    }
    mSlot = welcome.slot;
    mConnected = true;
    pGame->players = welcome.players;
    pGame->localPlayer = welcome.slot;
    pGame->gameType = welcome.gameType;
    pGame->fragLimit = welcome.fragLimit;
    pGame->timeLimitMin = welcome.timeLimitMin;
    pGame->tickMs = welcome.tickMs;
    pGame->police = welcome.police;
    pGame->gmp = welcome.gmp;
    pGame->sty = welcome.sty;
    pGame->scr = welcome.scr;
    printf("net: joined %s:%u as player %d of %d, %s\n", config.host.c_str(), config.port, mSlot, welcome.players, welcome.scr.c_str());
    return true;
}

void net::Client::OnSnapshot(BitReader& r)
{
    SnapshotHeader h;
    if (!ReadSnapshotHeader(r, h) || h.tick <= mLatestTick)
    {
        return; // malformed, or older than what we have
    }
    static const Snapshot kEmpty;
    const Snapshot* base = &kEmpty;
    if (h.baseTick != h.tick)
    {
        base = &mReceived[h.baseTick % kRing];
        if (base->tick != h.baseTick)
        {
            return; // a baseline we no longer have: the server sends a full one once our ack is old
        }
    }
    Snapshot snap;
    if (!ReadDelta(r, engine::NetSchema(), *base, snap))
    {
        return;
    }
    snap.tick = h.tick;
    mReceived[h.tick % kRing] = snap;
    mLatestTick = h.tick;
    mNewSnapshot = true;
}

// A simple stand-in player for tests: walks, turns now and then, shoots now and then (better bots: N6)
uint32_t net::Client::BotButtons()
{
    if (mFrame % 30 == 0)
    {
        mBotSeed = mBotSeed * 1103515245u + 12345u;
        const uint32_t r = mBotSeed >> 16;
        mBotButtons = 0x1; // forward
        mBotButtons |= (r % 4 == 0) ? 0x4 : (r % 4 == 1) ? 0x8 : 0; // left, right
        mBotButtons |= (r % 8 == 2) ? 0x10 : 0; // attack
    }
    return mBotButtons;
}

void net::Client::Frame()
{
    mFrame++;
    engine::ClientModeFrame();
    TransportEvent e;
    while (mTransport->Poll(e))
    {
        if (e.type == TransportEvent::kReceive && e.channel == kChannelUnreliable)
        {
            BitReader r(e.data.data(), e.data.size());
            if (r.Bits(8) == kMsgSnapshot)
            {
                OnSnapshot(r);
            }
        }
        else if (e.type == TransportEvent::kDisconnect && mConnected)
        {
            mConnected = false;
            printf("net: lost the server\n");
        }
    }
    if (!mConnected)
    {
        return;
    }
    if (mNewSnapshot)
    {
        const Snapshot& snap = mReceived[mLatestTick % kRing];
        if (mFrame % 300 == 0)
        {
            // How far this game's own player is from where the server has it (no prediction yet: up to a block)
            float x = 0, y = 0, d = -1;
            for (size_t i = 0; i < snap.entities.size(); i++)
            {
                const EntityState& p = snap.entities[i];
                if (p.type == engine::kEntityPlayer && (int)p.id == mSlot + 1 && engine::PlayerPosition(mSlot, &x, &y))
                {
                    const float dx = p.f[0] / 16384.0f - x, dy = p.f[1] / 16384.0f - y;
                    d = dx * dx + dy * dy;
                }
            }
            std::string players;
            for (size_t i = 0; i < snap.entities.size(); i++)
            {
                const EntityState& p = snap.entities[i];
                if (p.type == engine::kEntityPlayer)
                {
                    char one[64];
                    snprintf(one, sizeof(one), " %u:(%.1f,%.1f)", p.id - 1, p.f[0] / 16384.0f, p.f[1] / 16384.0f);
                    players += one;
                }
            }
            printf("net: frame %u, server tick %u, %zu entities, own player %.2f blocks from the server's; players%s\n", mFrame,
                   mLatestTick, snap.entities.size(), d >= 0 ? sqrtf(d) : -1.0f, players.c_str());
        }
        engine::Apply(snap, mSlot);
        mNewSnapshot = false;
    }
    uint32_t buttons = engine::LocalInput();
    if (mConfig.bot)
    {
        buttons = BotButtons();
        engine::SetPlayerInput(mSlot, buttons);
    }
    InputCmd cmd = {mFrame, buttons & 0x3FFFFF};
    mCmds.push_back(cmd);
    if (mCmds.size() > (size_t)kMaxCmdsPerPacket)
    {
        mCmds.erase(mCmds.begin());
    }
    BitWriter w;
    WriteCommands(w, mLatestTick, mCmds.data(), (int)mCmds.size());
    mTransport->Send(0, kChannelUnreliable, w.Data().data(), w.ByteCount(), false);
    mTransport->Flush();
}
