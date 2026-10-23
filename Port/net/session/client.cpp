// Port/net (docs/netcode.md, "Client"): joins a server, starts the same network game, sends its input every frame
// (repeating the last few commands), and puts the server's snapshots into its game.
#include "session.hpp"
#include <chrono>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <unordered_map>

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

void net::Client::Welcomed(const Welcome& welcome, engine::NetworkGame* pGame)
{
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
}

bool net::Client::StartReplay(const char* path, engine::NetworkGame* pGame, std::string* error)
{
    Welcome welcome;
    mReplay.reset(new ClientReplay());
    if (!mReplay->Open(path, &welcome))
    {
        *error = std::string("not a recording: ") + path;
        return false;
    }
    Welcomed(welcome, pGame);
    printf("net: replaying %s as player %d of %d, %s\n", path, mSlot, welcome.players, welcome.scr.c_str());
    return true;
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
    Welcomed(welcome, pGame);
    if (const char* path = getenv("GTA2_NET_RECORD"))
    {
        mRecorder.reset(new ClientRecorder());
        if (!mRecorder->Open(path, welcome))
        {
            printf("net: can't record to %s\n", path);
            mRecorder.reset();
        }
    }
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
    return (mRtt / 2 + mTickMs - 1) / mTickMs + 2;
}

// A snapshot of tick S: back to the game as it was after S, the server's state on top, then the ticks since then
// again with the inputs we used (other players: their last known input)
void net::Client::Reconcile(const Snapshot& snap)
{
    const uint32_t s = snap.tick;
    // The display error: what tick S showed of the cars and peds against the server's S
    const std::vector<engine::ReplicaPos>& shown = mShown[s % kRing];
    if (!shown.empty() && shown.back().id == 0xFFFFFFFFu && (uint32_t)shown.back().x == s)
    {
        size_t j = 0;
        for (size_t i = 0; i + 1 < shown.size(); i++)
        {
            for (j = 0; j < snap.entities.size() && snap.entities[j].id != shown[i].id; j++)
            {
            }
            if (j < snap.entities.size())
            {
                const float dx = snap.entities[j].f[0] / 16384.0f - shown[i].x, dy = snap.entities[j].f[1] / 16384.0f - shown[i].y;
                const float e = sqrtf(dx * dx + dy * dy);
                mShownErrorSum += e;
                mShownErrorCount++;
                mShownErrorMax = e > mShownErrorMax ? e : mShownErrorMax;
            }
        }
    }
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
        engine::ApplyScores(snap);
        return;
    }
    std::vector<engine::Pose> before; // what this frame would have shown, to blend the correction in
    engine::Poses(before);
    engine::RestoreGame(*saved);
    engine::DebugCheckPlayerCars("after the restore");
    engine::DebugOwnCarDiff(snap);
    engine::Apply(snap);
    engine::DebugCheckPlayerCars("after the apply");
    std::vector<uint32_t> buttons(mRemote);
    for (uint32_t t = s + 1; t <= mTick; t++)
    {
        buttons[mSlot] = mHistTick[t % kRing] == t ? mHistButtons[t % kRing] : 0;
        engine::ResimTick(buttons.data(), (int)buttons.size());
        engine::DebugCheckPlayerCars("after a resimulated tick");
        engine::SaveGame(*mSaves[t % kSaves], t);
        mResimTicks++;
    }
    mRollbacks++;
    engine::ApplyScores(snap);
    if (getenv("GTA2_NET_EXTRAP")) // off by default: the resimulation, from the server's state, does better
    {
        Extrapolate(snap);
    }
    Blend(before);
}

// The correction in steps: what is shown moves from where it was a part of the way to the corrected state each frame
// (GTA2_NET_BLEND, 0.3; 1: at once). The saves keep the corrected state, so the blend only touches the live tick
// before the next snapshot's resimulation; more than 2 blocks off (a respawn, a car entered) jumps.
void net::Client::Blend(const std::vector<engine::Pose>& shown)
{
    static const float alpha = getenv("GTA2_NET_BLEND") ? (float)atof(getenv("GTA2_NET_BLEND")) : 0.3f;
    if (alpha >= 1 || alpha <= 0)
    {
        return;
    }
    std::vector<engine::Pose> now;
    engine::Poses(now);
    std::unordered_map<uint32_t, const engine::Pose*> before;
    for (size_t i = 0; i < shown.size(); i++)
    {
        before[shown[i].id] = &shown[i];
    }
    const int32_t kFar = 2 << 14;
    for (size_t i = 0; i < now.size(); i++)
    {
        const engine::Pose& n = now[i];
        auto it = before.find(n.id);
        if (it == before.end())
        {
            continue;
        }
        const engine::Pose& b = *it->second;
        const int32_t dx = n.x - b.x, dy = n.y - b.y, dz = n.z - b.z;
        int32_t dr = ((n.rot - b.rot) % 1440 + 1440 + 720) % 1440 - 720; // the short way round
        if (abs(dx) > kFar || abs(dy) > kFar || abs(dz) > kFar || (!dx && !dy && !dz && !dr))
        {
            continue;
        }
        const engine::Pose p = {n.id, b.x + (int32_t)(dx * alpha), b.y + (int32_t)(dy * alpha), b.z + (int32_t)(dz * alpha),
                                ((b.rot + (int32_t)(dr * alpha)) % 1440 + 1440) % 1440};
        engine::SetPose(p);
    }
}

// Cars and peds the client doesn't drive: on from S to our tick by their speed over the last snapshots
void net::Client::Extrapolate(const Snapshot& snap)
{
    for (uint32_t back = 1; back <= 4 && back < snap.tick; back++)
    {
        const Snapshot& prev = mReceived[(snap.tick - back) % kRing];
        if (prev.tick == snap.tick - back)
        {
            engine::Extrapolate(snap, prev, (int)(mTick - snap.tick));
            return;
        }
    }
}

void net::Client::Frame()
{
    mFrame++;
    engine::DebugSetFrame(mFrame);
    engine::DebugSoundChannels(mFrame);
    // What arrives this frame: from the network, or the recording
    std::vector<TransportEvent> events;
    uint32_t recordedFrame = 0, recordedInput = 0;
    if (mReplay)
    {
        if (!mReplay->Frame(&recordedFrame, &mRtt, &recordedInput, events))
        {
            printf("net: the replay ends at frame %u\n", mFrame);
            fflush(stdout);
            exit(0);
        }
    }
    else
    {
        TransportEvent e;
        while (mTransport->Poll(e))
        {
            events.push_back(e);
        }
        mRtt = mTransport->RoundTripMs(0);
    }
    // This frame's input first: a restore below would overwrite the game's input buffers
    uint32_t local = engine::LocalInput();
    if (mReplay)
    {
        local = recordedInput;
    }
    static const bool sInputLog = getenv("GTA2_NET_INPUTLOG") != NULL;
    static uint32_t sLastInput = ~0u;
    if (sInputLog && local != sLastInput)
    {
        printf("net: frame %u, input 0x%x\n", mFrame, local);
        sLastInput = local;
    }
    else if (!mConfig.bot.empty() && engine::GameRunning())
    {
        if (!mBot)
        {
            mBot = CreateBotBrain(mConfig.bot);
            mNavMap.reset(new engine::NavMap());
            engine::BuildNavMap(*mNavMap);
            if (const char* dump = getenv("GTA2_NET_NAVDUMP")) // the bots' map as a picture: grey by height, black blocked
            {
                if (FILE* f = fopen(dump, "wb"))
                {
                    fprintf(f, "P5 %d %d 255\n", engine::NavMap::kSize, engine::NavMap::kSize);
                    for (int c = 0; c < engine::NavMap::kSize * engine::NavMap::kSize; c++)
                    {
                        const int h = mNavMap->height[c], w = mNavMap->ways[c];
                        fputc(h == 255 || !w ? 0 : 60 + h * 24 + (w == 15 ? 20 : 0), f);
                    }
                    fclose(f);
                }
            }
        }
        FillBotView(mBotView, mReceived[mLatestTick % kRing], mSlot, mFrame, mNavMap.get());
        local = mBot ? mBot->Think(mBotView) : 0;
    }
    local &= 0x3FFFFF;
    if (mRecorder)
    {
        mRecorder->Frame(mFrame, mRtt, local, events);
    }
    engine::ClientModeFrame();
    if (mSaves.empty())
    {
        for (int i = 0; i < kSaves; i++)
        {
            mSaves.emplace_back(new engine::SavedWorld());
        }
        mRemote.assign(engine::PlayerCount(), 0);
    }
    mWasWasted.resize(mRemote.size());
    mLocalDeaths.resize(mRemote.size());
    for (size_t i = 0; i < mRemote.size(); i++)
    {
        const bool w = engine::PlayerWasted((int)i);
        mLocalDeaths[i] += w && !mWasWasted[i];
        mWasWasted[i] = w;
    }
    {
        const Snapshot& latest = mReceived[mLatestTick % kRing];
        for (size_t i = 0; i < latest.entities.size(); i++)
        {
            int p = 0;
            uint32_t b = 0;
            if (engine::PlayerOf(latest.entities[i], &p, &b))
            {
                mCarFrames++;
                mCarMismatch += (engine::PlayerCarId(latest.entities[i]) != 0) != engine::PlayerInCar(p);
            }
        }
    }
    engine::DebugCheckPlayerCars("after the frame's tick");
    if (mSynced) // the tick that just ran
    {
        mTick++;
        engine::SaveGame(*mSaves[mTick % kSaves], mTick);
        engine::PlayerPosition(mSlot, &mPredX[mTick % kRing], &mPredY[mTick % kRing]);
        engine::ReplicaPositions(mShown[mTick % kRing]);
        mShown[mTick % kRing].push_back(engine::ReplicaPos{0xFFFFFFFFu, (float)mTick, 0}); // its tick, last
    }
    for (size_t i = 0; i < events.size(); i++)
    {
        const TransportEvent& e = events[i];
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
            engine::ApplyScores(snap);
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
                   "%.1f ticks resimulated each, prediction error up to %.2f blocks, cars and peds shown %.2f blocks off (up to %.2f)\n",
                   mFrame, mTick, mLatestTick, mSlack, snap.entities.size(), cars, peds, mRollbacks,
                   mRollbacks ? (float)mResimTicks / mRollbacks : 0.0f, mMaxError,
                   mShownErrorCount ? (float)(mShownErrorSum / mShownErrorCount) : 0.0f, mShownErrorMax);
            std::string local;
            for (size_t i = 0; i < mLocalDeaths.size(); i++)
            {
                local += " " + std::to_string(mLocalDeaths[i]);
            }
            printf("net: frame %u, frags/deaths(health)%s; deaths shown here:%s\n", mFrame, Scoreboard(snap).c_str(), local.c_str());
            int poolCars = 0, poolPeds = 0, poolChars = 0, sent = 0, driven = 0, same = 0;
            engine::PoolUse(&poolCars, &poolPeds, &poolChars);
            engine::DriverStats(snap, &sent, &driven, &same);
            int traffic = 0, trafficHere = 0;
            engine::TrafficDriverStats(snap, &traffic, &trafficHere);
            printf("net: frame %u, drivers: %d peds in the snapshot, %d of their cars driven here, %d by the same ped; %d traffic cars, %d "
                   "with the traffic driver here\n",
                   mFrame, sent, driven, same, traffic, trafficHere);
            printf("net: frame %u, this game has %d cars, %d peds, %d chars; in or out of a car differently from the newest snapshot: "
                   "%.1f%% of player frames\n",
                   mFrame, poolCars, poolPeds, poolChars, mCarFrames ? 100.0 * mCarMismatch / mCarFrames : 0.0);
            mCarFrames = mCarMismatch = 0;
            if (mBot)
            {
                printf("net: frame %u, bot %s\n", mFrame, mBot->Describe().c_str());
            }
            mRollbacks = mResimTicks = 0;
            mMaxError = 0;
            mShownErrorSum = 0;
            mShownErrorCount = 0;
            mShownErrorMax = 0;
        }
    }
    // Keep our commands about two ticks early: a little faster when they are late, slower when far ahead
    if (mSynced)
    {
        tickMs = !mSlackKnown ? mTickMs : mSlack < 1 ? mTickMs - mTickMs / 10 : mSlack > 4 ? mTickMs + mTickMs / 10 : mTickMs;
    }
    engine::SetTickMs(mReplay ? 1 : tickMs); // every frame: a restore brings back the game's tick length too (a replay: no waiting)

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
    if (mTransport)
    {
        BitWriter w;
        WriteCommands(w, mLatestTick, cmds, n);
        mTransport->Send(0, kChannelUnreliable, w.Data().data(), w.ByteCount(), false);
        mTransport->Flush();
    }
}
