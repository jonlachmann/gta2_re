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
const int kSaves = 32; // ticks a snapshot may be behind and still be reconciled (about 1 s)

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

net::Client::Client()
{
}

net::Client::~Client()
{
}

bool net::Client::Connect(const ClientConfig& config, engine::NetworkGame* pGame, std::string* error)
{
    mConfig = config;
    mTransport.reset(CreateClientTransport(config.host.c_str(), config.port, error));
    if (!mTransport)
    {
        return false;
    }
    if (config.lagMs || config.jitterMs || config.lossPercent)
    {
        mTransport.reset(CreateConditioner(mTransport.release(), config.lagMs, config.jitterMs, config.lossPercent));
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
    mTickMs = welcome.tickMs ? welcome.tickMs : 33;
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
    mSlack = (int)h.lastCmdTick - (int)(h.tick + 1);
    mSlackKnown = mSynced && h.lastCmdTick > mJumpTick; // it has heard of the ticks since our last jump
    mNewSnapshot = true;
}

uint32_t net::Client::Lead() const
{
    // Half the round trip in ticks, and two to spare: our command for a tick should arrive just before it runs
    return (mTransport->RoundTripMs(0) / 2 + mTickMs - 1) / mTickMs + 2;
}

// A snapshot of tick S: back to the game as it was after S, the server's state on top, then the ticks since then
// again with the inputs we used (other players: their last known input)
void net::Client::Reconcile(const Snapshot& snap)
{
    const uint32_t s = snap.tick;
    for (size_t i = 0; i < snap.entities.size(); i++)
    {
        int player = 0;
        uint32_t buttons = 0;
        if (engine::PlayerOf(snap.entities[i], &player, &buttons) && player >= 0 && player < (int)mRemote.size())
        {
            mRemote[player] = buttons;
            if (player == mSlot && mHistTick[s % kRing] == s)
            {
                // The prediction error: where we had ourselves at S against where the server has us
                const float dx = snap.entities[i].f[0] / 16384.0f - mPredX[s % kRing];
                const float dy = snap.entities[i].f[1] / 16384.0f - mPredY[s % kRing];
                const float e = sqrtf(dx * dx + dy * dy);
                mMaxError = e > mMaxError ? e : mMaxError;
            }
        }
    }
    engine::SavedWorld* saved = s <= mTick && s + kSaves > mTick ? mSaves[s % kSaves].get() : NULL;
    if (!saved || !saved->valid || saved->tick != s)
    {
        engine::Apply(snap); // too old (or too new) to reconcile: take it as it is
        return;
    }
    engine::RestoreGame(*saved);
    engine::Apply(snap);
    std::vector<uint32_t> buttons(mRemote);
    for (uint32_t t = s + 1; t <= mTick; t++)
    {
        buttons[mSlot] = mHistTick[t % kRing] == t ? mHistButtons[t % kRing] : 0;
        engine::ResimTick(buttons.data(), (int)buttons.size());
        engine::SaveGame(*mSaves[t % kSaves], t);
        mResimTicks++;
    }
    mRollbacks++;
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
    // This frame's input first: a restore below would overwrite the game's input buffers
    const uint32_t local = (mConfig.bot ? BotButtons() : engine::LocalInput()) & 0x3FFFFF;
    engine::ClientModeFrame();
    if (mSaves.empty())
    {
        for (int i = 0; i < kSaves; i++)
        {
            mSaves.emplace_back(new engine::SavedWorld());
        }
        mRemote.assign(engine::PlayerCount(), 0);
    }
    if (mSynced) // the tick that just ran
    {
        mTick++;
        engine::SaveGame(*mSaves[mTick % kSaves], mTick);
        engine::PlayerPosition(mSlot, &mPredX[mTick % kRing], &mPredY[mTick % kRing]);
    }
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
    int tickMs = mTickMs;
    if (mNewSnapshot)
    {
        const Snapshot& snap = mReceived[mLatestTick % kRing];
        // How far our commands are from two ticks early. ENet's round trip doesn't see a link conditioner, and the
        // server's word is better anyway: it says which tick's command it had last
        const int off = mSynced && mSlackKnown ? mSlack - 2 : 0;
        if (!mSynced || mLatestTick > mTick || off < -3 || off > 12)
        {
            // Not running ahead of the server (yet, or any more), or far off: jump to where we should be
            mTick = !mSynced || mLatestTick > mTick ? mLatestTick + Lead() : mTick - off;
            mSynced = true;
            mSlackKnown = false; // until a snapshot answers commands for the new ticks
            mJumpTick = mTick;
            for (size_t i = 0; i < mSaves.size(); i++)
            {
                mSaves[i]->valid = false;
            }
            engine::Apply(snap);
        }
        else
        {
            Reconcile(snap);
        }
        mNewSnapshot = false;
        if (mFrame % 300 == 0 || mFrame < 3)
        {
            int cars = 0, peds = 0;
            float drift = 0;
            engine::ReplicaStats(snap, &cars, &peds, &drift);
            printf("net: frame %u, tick %u (server %u, our commands %d ticks early), %zu entities (%d cars, %d peds), %u rollbacks, "
                   "%.1f ticks resimulated each, prediction error up to %.2f blocks\n",
                   mFrame, mTick, mLatestTick, mSlack, snap.entities.size(), cars, peds, mRollbacks,
                   mRollbacks ? (float)mResimTicks / mRollbacks : 0.0f, mMaxError);
            mRollbacks = mResimTicks = 0;
            mMaxError = 0;
        }
    }
    // Keep our commands about two ticks early: a little faster when they are late, slower when far ahead
    if (mSynced)
    {
        tickMs = !mSlackKnown ? mTickMs : mSlack < 1 ? mTickMs - mTickMs / 10 : mSlack > 4 ? mTickMs + mTickMs / 10 : mTickMs;
    }
    engine::SetTickMs(tickMs); // every frame: a restore brings back the game's tick length too

    // The next tick's inputs: ours (predicted and sent), the others' last known
    const uint32_t next = mTick + 1;
    mHistTick[next % kRing] = next;
    mHistButtons[next % kRing] = local;
    for (size_t i = 0; i < mRemote.size(); i++)
    {
        engine::SetPlayerInput((int)i, (int)i == mSlot ? local : mRemote[i]);
    }
    if (!mSynced)
    {
        return;
    }
    InputCmd cmds[kMaxCmdsPerPacket];
    int n = 0;
    for (uint32_t t = next >= (uint32_t)kMaxCmdsPerPacket ? next - kMaxCmdsPerPacket + 1 : 1; t <= next; t++)
    {
        if (mHistTick[t % kRing] == t)
        {
            cmds[n].tick = t;
            cmds[n].buttons = mHistButtons[t % kRing];
            n++;
        }
        else
        {
            n = 0; // only consecutive ticks
        }
    }
    BitWriter w;
    WriteCommands(w, mLatestTick, cmds, n);
    mTransport->Send(0, kChannelUnreliable, w.Data().data(), w.ByteCount(), false);
    mTransport->Flush();
}
