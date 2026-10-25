// Port/net (docs/netcode.md, "Client"): joins a server, starts the same network game, sends its input every frame
// (repeating the last few commands), and puts the server's snapshots into its game.
#include "session.hpp"
#include <algorithm>
#include <chrono>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <unordered_map>

namespace
{
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

void net::CorrectionStats::Print(uint32_t frame, const char* what) const
{
    printf("net: frame %u, %s' corrections: %u, %.2f blocks on average, up to %.2f, %u over 2 blocks (jumped)\n", frame, what, count,
           count ? sum / count : 0.0f, max, jumps);
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
    mJerk.PrintJumps(Env().jerkLog >= 2);
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
    // GTA2_NET_RECORD=<file>, or off; a person's session is recorded anyway, a bot's only when asked
    const char* record = Env().record;
    const std::string path = record ? (strcmp(record, "off") ? record : "") : mConfig.bot.empty() ? NextRecordingPath() : "";
    if (!path.empty())
    {
        mRecorder.reset(new ClientRecorder());
        if (!mRecorder->Open(path.c_str(), welcome))
        {
            printf("net: can't record to %s\n", path.c_str());
            mRecorder.reset();
        }
        else
        {
            printf("net: recording to %s (F12 marks a moment)\n", path.c_str());
        }
    }
    printf("net: joined %s:%u as player %d of %d, %s\n", config.host.c_str(), config.port, mSlot, welcome.players, welcome.scr.c_str());
    return true;
}

void net::Client::OnSnapshotPart(BitReader& r)
{
    uint32_t tick = 0;
    int index = 0, count = 0;
    std::vector<uint8_t> piece;
    if (!ReadSnapshotPart(r, &tick, &index, &count, piece) || tick <= mLatestTick)
    {
        return;
    }
    Parts& p = mParts[tick % 4];
    if (p.tick != tick || p.count != count)
    {
        p.tick = tick; // a newer snapshot's: one that never came whole is dropped
        p.count = count;
        p.have = 0;
        p.pieces.assign((size_t)count, std::vector<uint8_t>());
    }
    if (!p.pieces[(size_t)index].empty())
    {
        return; // had it
    }
    p.pieces[(size_t)index] = piece;
    if (++p.have < p.count)
    {
        return;
    }
    std::vector<uint8_t> message;
    for (int i = 0; i < count; i++)
    {
        message.insert(message.end(), p.pieces[(size_t)i].begin(), p.pieces[(size_t)i].end());
    }
    p.tick = 0;
    BitReader whole(message.data(), message.size());
    if (whole.Bits(8) == kMsgSnapshot)
    {
        OnSnapshot(whole);
    }
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

// The display error: what tick S showed of the cars and peds against the server's S
void net::Client::MeasureShownError(const Snapshot& snap)
{
    const TickRecord& rec = mHist[snap.tick % kRing];
    if (rec.shownTick != snap.tick)
    {
        return;
    }
    for (const engine::ReplicaPos& shown : rec.shown)
    {
        size_t j = 0;
        while (j < snap.entities.size() && snap.entities[j].id != shown.id)
        {
            j++;
        }
        if (j < snap.entities.size())
        {
            const float dx = engine::Blocks(snap.entities[j].f[0]) - shown.x, dy = engine::Blocks(snap.entities[j].f[1]) - shown.y;
            const float e = sqrtf(dx * dx + dy * dy);
            mStats.shownErrorSum += e;
            mStats.shownErrorCount++;
            mStats.shownErrorMax = e > mStats.shownErrorMax ? e : mStats.shownErrorMax;
        }
    }
}

// The other players' inputs (we predict them with these), the marker, and the prediction error: where we had ourselves
// at S against where the server has us
void net::Client::TakePlayerStates(const Snapshot& snap)
{
    const TickRecord& rec = mHist[snap.tick % kRing];
    for (const EntityState& e : snap.entities)
    {
        int player = 0;
        uint32_t buttons = 0;
        if (!engine::PlayerOf(e, &player, &buttons) || player < 0 || player >= (int)mRemote.size())
        {
            continue;
        }
        mRemote[player] = buttons;
        if (player == mSlot && Env().marker)
        {
            engine::SetServerMarker(e.f[0], e.f[1], e.f[2]);
        }
        if (player == mSlot && rec.tick == snap.tick)
        {
            const float dx = engine::Blocks(e.f[0]) - rec.predX, dy = engine::Blocks(e.f[1]) - rec.predY;
            const float err = sqrtf(dx * dx + dy * dy);
            mStats.maxError = err > mStats.maxError ? err : mStats.maxError;
        }
    }
}

// A snapshot of tick S: back to the game as it was after S, the server's state on top, then the ticks since then
// again with the inputs we used (other players: their last known input)
void net::Client::Reconcile(const Snapshot& snap)
{
    const uint32_t s = snap.tick;
    MeasureShownError(snap);
    TakePlayerStates(snap);
    engine::SavedWorld* saved = s <= mTick && s + kSaves > mTick ? mSaves[s % kSaves].get() : NULL;
    if (mAloneFrom)
    {
        CompareAlone(snap, saved);
        return;
    }
    if (!saved || !saved->valid || saved->tick != s)
    {
        engine::Apply(snap); // too old (or too new) to reconcile: take it as it is
        engine::ApplyScores(snap);
        return;
    }
    std::vector<engine::Pose> before; // what this frame would have shown, to blend the correction in
    engine::Poses(before);
    const engine::LocalChat chat = engine::TakeLocalChat();
    engine::RestoreGame(*saved);
    engine::DebugCheckPlayerCars("after the restore");
    engine::DebugOwnCarDiff(snap);
    if (Env().compare)
    {
        engine::ComparePrediction(snap);
    }
    engine::Apply(snap);
    engine::DebugCheckPlayerCars("after the apply");
    if (Env().divergeTick && s >= Env().divergeTick)
    {
        engine::CompareDivergence(snap, 0); // as applied: what the snapshot didn't carry over
        engine::DumpPeds(s, false);
        mAloneFrom = s;
        engine::StartRandLog(); // from here on each tick runs once (the resimulation below, then live)
        printf("net: divergence check, on our own from server tick %u (our tick %u)\n", s, mTick);
    }
    std::vector<uint32_t> buttons(mRemote);
    for (uint32_t t = s + 1; t <= mTick; t++)
    {
        buttons[mSlot] = mHist[t % kRing].tick == t ? mHist[t % kRing].buttons : 0;
        engine::ResimTick(buttons.data(), (int)buttons.size());
        engine::DebugCheckPlayerCars("after a resimulated tick");
        engine::SaveGame(*mSaves[t % kSaves], t);
        mStats.resimTicks++;
    }
    engine::PutLocalChat(chat);
    mStats.rollbacks++;
    engine::ApplyScores(snap);
    Blend(before);
}

// The divergence check, once on our own: each snapshot against our game at its tick (a save), then back to now
void net::Client::CompareAlone(const Snapshot& snap, engine::SavedWorld* saved)
{
    const uint32_t s = snap.tick;
    if (saved && saved->valid && saved->tick == s && s > mAloneFrom)
    {
        engine::RestoreGame(*saved);
        engine::CompareDivergence(snap, s - mAloneFrom);
        engine::DumpPeds(s, false);
        engine::RestoreGame(*mSaves[mTick % kSaves]);
    }
    if (s >= mAloneFrom + Env().divergeTicks)
    {
        engine::PrintDivergence();
        exit(0);
    }
}

// GTA2_NET_CORRLOG: corrections of over half a block or 45 degrees (players: a quarter block)
void net::Client::LogCorrection(const engine::Pose& n, int32_t dx, int32_t dy, int32_t dr) const
{
    const int32_t kFar = 2 << 14;
    if (!Env().corrLog || !(abs(dx) > 8192 || abs(dy) > 8192 || abs(dr) > 180 || (engine::IsPlayerId(n.id) && (abs(dx) > 4096 || abs(dy) > 4096))))
    {
        return;
    }
    printf("net: frame %u, correction of %u: %.2f blocks, %d degrees%s%s\n", mFrame, n.id, sqrtf((float)dx * dx + (float)dy * dy) / 16384.0f,
           dr / 4, abs(dx) > kFar || abs(dy) > kFar ? " (jumped)" : "",
           engine::IsPlayerId(n.id) ? ("; " + engine::DebugPlayerCar((int)n.id - 1, mReceived[mLatestTick % kRing])).c_str() : "");
}

// The correction in steps: what is shown moves from where it was a part of the way to the corrected state each frame
// (GTA2_NET_BLEND, 0.2; 1: at once). The saves keep the corrected state, so the blend only touches the live tick
// before the next snapshot's resimulation; more than 2 blocks off (a respawn, a car entered) jumps.
void net::Client::Blend(const std::vector<engine::Pose>& shown)
{
    const float alpha = Env().blend;
    if (alpha >= 1 || alpha <= 0)
    {
        return;
    }
    std::vector<engine::Pose> now;
    engine::Poses(now);
    std::unordered_map<uint32_t, const engine::Pose*> before;
    for (const engine::Pose& p : shown)
    {
        before[p.id] = &p;
    }
    const int32_t kFar = 2 << 14;
    for (const engine::Pose& n : now)
    {
        auto it = before.find(n.id);
        if (it == before.end())
        {
            continue;
        }
        const engine::Pose& b = *it->second;
        const int32_t dx = n.x - b.x, dy = n.y - b.y, dz = n.z - b.z;
        const int32_t dr = ((n.rot - b.rot) % 1440 + 1440 + 720) % 1440 - 720; // the short way round
        const bool jumped = abs(dx) > kFar || abs(dy) > kFar;
        if (dx || dy)
        {
            CorrectionStats* stats = engine::IsPlayerId(n.id) ? &mStats.players : engine::IsCarId(n.id) ? &mStats.cars : NULL;
            if (stats)
            {
                stats->Add(sqrtf((float)dx * dx + (float)dy * dy) / 16384.0f, jumped);
            }
        }
        LogCorrection(n, dx, dy, dr);
        if (jumped || abs(dz) > kFar || (!dx && !dy && !dz && !dr))
        {
            continue;
        }
        const engine::Pose p = {n.id, b.x + (int32_t)(dx * alpha), b.y + (int32_t)(dy * alpha), b.z + (int32_t)(dz * alpha),
                                ((b.rot + (int32_t)(dr * alpha)) % 1440 + 1440) % 1440};
        engine::SetPose(p);
    }
}

// What arrives this frame: from the network, or the recording (false: the recording is over)
bool net::Client::TakeEvents(std::vector<TransportEvent>& events, uint32_t* pRecordedInput)
{
    if (mReplay)
    {
        uint32_t recordedFrame = 0;
        return mReplay->Frame(&recordedFrame, &mRtt, pRecordedInput, events);
    }
    TransportEvent e;
    while (mTransport->Poll(e))
    {
        events.push_back(e);
    }
    mRtt = mTransport->RoundTripMs(0);
    return true;
}

void net::Client::StartBot()
{
    mBot = CreateBotBrain(mConfig.bot);
    mNavMap.reset(new engine::NavMap());
    engine::BuildNavMap(*mNavMap);
    if (Env().navDump)
    {
        engine::DumpNavMap(*mNavMap, Env().navDump);
    }
}

// This frame's input: the controls', the recording's, or the bot's. First thing: a restore would overwrite the game's
// input buffers
uint32_t net::Client::ChooseInput(uint32_t recordedInput)
{
    uint32_t local = mReplay ? recordedInput : engine::LocalInput();
    if (!mConfig.bot.empty() && engine::GameRunning())
    {
        if (!mBot)
        {
            StartBot();
        }
        FillBotView(mBotView, mReceived[mLatestTick % kRing], mSlot, mFrame, mNavMap.get());
        local = mBot ? mBot->Think(mBotView) : 0;
    }
    local &= 0x3FFFFF;
    // F12 (a key the game doesn't use, here as its DirectInput code and key-down bit: Source/input_bits.hpp) marks
    // the moment, in play and in its replay alike
    const uint32_t kKeyDown = 0x200000, kMarkKey = 0x58; // DIK_F12
    if ((local & kKeyDown) && ((local >> 12) & 0x1FF) == kMarkKey)
    {
        if (mFrame != mMarkFrame + 1) // held: one mark
        {
            printf("net: mark %d at frame %u, tick %u\n", ++mMarks, mFrame, mTick);
        }
        mMarkFrame = mFrame;
    }
    if (Env().inputLog && local != mLastInput)
    {
        printf("net: frame %u, input 0x%x\n", mFrame, local);
        mLastInput = local;
    }
    return local;
}

// For the test log: the deaths this game carried out, and how often it had a player in or out of a car otherwise than
// the newest snapshot
void net::Client::CountTestStats()
{
    mWasWasted.resize(mRemote.size());
    mLocalDeaths.resize(mRemote.size());
    for (size_t i = 0; i < mRemote.size(); i++)
    {
        const bool w = engine::PlayerWasted((int)i);
        mLocalDeaths[i] += w && !mWasWasted[i];
        mWasWasted[i] = w;
    }
    for (const EntityState& e : mReceived[mLatestTick % kRing].entities)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(e, &p, &b))
        {
            mStats.carFrames++;
            mStats.carMismatch += (engine::PlayerCarId(e) != 0) != engine::PlayerInCar(p);
        }
    }
}

// The tick that just ran: saved, and what it showed
void net::Client::RecordTick()
{
    mTick++;
    engine::SaveGame(*mSaves[mTick % kSaves], mTick);
    TickRecord& rec = mHist[mTick % kRing];
    engine::PlayerPosition(mSlot, &rec.predX, &rec.predY);
    engine::ReplicaPositions(rec.shown);
    rec.shownTick = mTick;
}

void net::Client::HandleEvents(const std::vector<TransportEvent>& events)
{
    for (const TransportEvent& e : events)
    {
        if (e.type == TransportEvent::kReceive && e.channel == kChannelUnreliable)
        {
            BitReader r(e.data.data(), e.data.size());
            const uint32_t type = r.Bits(8);
            if (type == kMsgSnapshot)
            {
                OnSnapshot(r);
            }
            else if (type == kMsgSnapshotPart)
            {
                OnSnapshotPart(r);
            }
        }
        else if (e.type == TransportEvent::kReceive)
        {
            BitReader r(e.data.data(), e.data.size());
            if (r.Bits(8) == kMsgMapChange)
            {
                OnMapChange(r);
            }
        }
        else if (e.type == TransportEvent::kDisconnect && mConnected)
        {
            mConnected = false;
            printf("net: lost the server\n");
        }
    }
}

// The server's next map: this game ends with the server's (after the same pause) and the next starts on it
void net::Client::OnMapChange(BitReader& r)
{
    MapChange m;
    if (ReadMapChange(r, m))
    {
        printf("net: frame %u, the next map: %s\n", mFrame, m.scr.c_str());
        engine::ChangeMap(m.gmp, m.sty, m.scr, 90);
        mServerGame = m.game; // from now on: the old game's snapshots would put its things into the next map's
    }
}

void net::Client::StartOver()
{
    mGeneration = engine::GameGeneration();
    mSynced = false;
    mNewSnapshot = false;
    for (Snapshot& s : mReceived)
    {
        s = Snapshot();
    }
    for (Parts& p : mParts)
    {
        p = Parts();
    }
    std::fill(mRemote.begin(), mRemote.end(), 0u);
    mBot.reset(); // and its map of the old one
    mNavMap.reset();
    printf("net: frame %u, a new game\n", mFrame);
}

// Not running ahead of the server (yet, or any more), or far off: jump to where we should be
void net::Client::Resync(const Snapshot& snap)
{
    const int off = mSynced && mSlackKnown ? mSlack - 2 : 0;
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

// A new round (its number changed in a snapshot): who won the last one, on the HUD; and when time is short
void net::Client::AnnounceRound()
{
    const engine::Round& now = engine::CurrentRound();
    std::string line;
    if (now.number != mRound.number && now.number > 1)
    {
        line = now.winner ? engine::PlayerName(now.winner - 1) + " wins round " + std::to_string(now.number - 1)
                          : "Round " + std::to_string(now.number - 1) + " is a draw";
    }
    else if (now.number == mRound.number && now.secondsLeft < mRound.secondsLeft && (now.secondsLeft == 60 || now.secondsLeft == 10))
    {
        line = std::to_string(now.secondsLeft) + " seconds left";
    }
    if (!line.empty())
    {
        engine::ShowHudMessage(line.c_str());
        printf("net: frame %u, %s\n", mFrame, line.c_str());
    }
    mRound = now;
}

void net::Client::TakeSnapshot()
{
    const Snapshot& snap = mReceived[mLatestTick % kRing];
    const int game = engine::SnapshotGame(snap);
    if (mServerGame < 0)
    {
        mServerGame = game;
    }
    if (game != mServerGame || engine::MapChangePending())
    {
        mNewSnapshot = false; // the old map's (the server's pause, or this game ending): not for this one
        return;
    }
    // How far our commands are from two ticks early. ENet's round trip doesn't see a link conditioner, and the server's
    // word is better anyway: it says which tick's command it had last
    const int off = mSynced && mSlackKnown ? mSlack - 2 : 0;
    if (!mAloneFrom && (!mSynced || mLatestTick > mTick || off < -3 || off > 12))
    {
        Resync(snap);
    }
    else
    {
        Reconcile(snap);
    }
    mNewSnapshot = false;
    if (mFrame % 300 == 0 || mFrame < 3)
    {
        PrintStats(snap);
    }
}

void net::Client::PrintStats(const Snapshot& snap)
{
    if (Env().compare)
    {
        engine::PrintPredictionStats(mFrame);
    }
    int cars = 0, peds = 0;
    float drift = 0;
    engine::ReplicaStats(snap, &cars, &peds, &drift);
    const Stats& st = mStats;
    printf("net: frame %u, tick %u (server %u, our commands %d ticks early), %zu entities (%d cars, %d peds), %u rollbacks, "
           "%.1f ticks resimulated each, prediction error up to %.2f blocks, cars and peds shown %.2f blocks off (up to %.2f)\n",
           mFrame, mTick, mLatestTick, mSlack, snap.entities.size(), cars, peds, st.rollbacks, st.rollbacks ? (float)st.resimTicks / st.rollbacks : 0.0f,
           st.maxError, st.shownErrorCount ? (float)(st.shownErrorSum / st.shownErrorCount) : 0.0f, st.shownErrorMax);
    std::string local;
    for (size_t i = 0; i < mLocalDeaths.size(); i++)
    {
        local += " " + std::to_string(mLocalDeaths[i]);
    }
    printf("net: frame %u, frags/deaths(health)%s; deaths shown here:%s\n", mFrame, Scoreboard(snap).c_str(), local.c_str());
    int poolCars = 0, poolPeds = 0, poolChars = 0, sent = 0, driven = 0, same = 0, traffic = 0, trafficHere = 0;
    engine::PrintReplicaLog();
    engine::PoolUse(&poolCars, &poolPeds, &poolChars);
    engine::DriverStats(snap, &sent, &driven, &same);
    engine::TrafficDriverStats(snap, &traffic, &trafficHere);
    printf("net: frame %u, drivers: %d peds in the snapshot, %d of their cars driven here, %d by the same ped; %d traffic cars, %d "
           "with the traffic driver here\n",
           mFrame, sent, driven, same, traffic, trafficHere);
    printf("net: frame %u, this game has %d cars, %d peds, %d chars; in or out of a car differently from the newest snapshot: "
           "%.1f%% of player frames\n",
           mFrame, poolCars, poolPeds, poolChars, st.carFrames ? 100.0 * st.carMismatch / st.carFrames : 0.0);
    if (mBot)
    {
        printf("net: frame %u, bot %s\n", mFrame, mBot->Describe().c_str());
    }
    st.players.Print(mFrame, "players");
    st.cars.Print(mFrame, "cars");
    mStats = Stats();
}

// GTA2_NET_JERKLOG: what can be seen (within 12 blocks; a camera shows 6 to 8)
void net::Client::FeedJerk()
{
    if (!Env().jerkLog || !mSynced)
    {
        return;
    }
    std::vector<engine::Pose> all, shown;
    engine::Poses(all);
    float lx = 0, ly = 0;
    engine::PlayerPosition(mSlot, &lx, &ly);
    for (const engine::Pose& q : all)
    {
        const float dx = engine::Blocks(q.x) - lx, dy = engine::Blocks(q.y) - ly;
        if (dx * dx + dy * dy < 12 * 12)
        {
            shown.push_back(q);
        }
    }
    mJerk.Feed(mFrame, shown, (uint32_t)mSlot + 1);
    if (mFrame % 900 == 0)
    {
        mJerk.Report("client", mFrame);
    }
}

// Keep our commands about two ticks early: a little faster when they are late, slower when far ahead
int net::Client::PacedTickMs() const
{
    if (!mSynced || !mSlackKnown)
    {
        return mTickMs;
    }
    return mSlack < 1 ? mTickMs - mTickMs / 10 : mSlack > 4 ? mTickMs + mTickMs / 10 : mTickMs;
}

// The next tick's inputs: ours (predicted and sent), the others' last known
void net::Client::SetNextInputs(uint32_t local)
{
    const uint32_t next = mTick + 1;
    mHist[next % kRing].tick = next;
    mHist[next % kRing].buttons = local;
    for (size_t i = 0; i < mRemote.size(); i++)
    {
        engine::SetPlayerInput((int)i, (int)i == mSlot ? local : mRemote[i]);
    }
}

// The commands for the last ticks, consecutive, up to the next one
void net::Client::SendCommands()
{
    const uint32_t next = mTick + 1;
    InputCmd cmds[kMaxCmdsPerPacket];
    int n = 0;
    for (uint32_t t = next >= (uint32_t)kMaxCmdsPerPacket ? next - kMaxCmdsPerPacket + 1 : 1; t <= next; t++)
    {
        if (mHist[t % kRing].tick == t)
        {
            cmds[n].tick = t;
            cmds[n].buttons = mHist[t % kRing].buttons;
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

void net::Client::Leave()
{
    mTransport.reset(); // its destructor disconnects (enet_peer_disconnect_now)
}

void net::Client::Frame()
{
    mFrame++;
    engine::KeepMapChange();
    if (engine::GameGeneration() != mGeneration)
    {
        StartOver();
    }
    engine::DebugSetFrame(mFrame);
    engine::DebugSoundChannels(mFrame);
    engine::DebugAnimLog(mFrame);
    engine::DebugSelfLog(mFrame);
    if (Env().animLog && mFrame % 3 == 0)
    {
        printf("net: frame %u, remote buttons:", mFrame);
        for (size_t i = 0; i < mRemote.size(); i++)
        {
            printf(" 0x%x", mRemote[i]);
        }
        printf("\n");
    }
    std::vector<TransportEvent> events;
    uint32_t recordedInput = 0;
    if (!TakeEvents(events, &recordedInput))
    {
        printf("net: the replay ends at frame %u\n", mFrame);
        fflush(stdout);
        exit(0);
    }
    const uint32_t local = ChooseInput(recordedInput);
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
    CountTestStats();
    engine::DebugCheckPlayerCars("after the frame's tick");
    if (mSynced)
    {
        RecordTick();
    }
    HandleEvents(events);
    if (!mConnected)
    {
        return;
    }
    if (mNewSnapshot)
    {
        TakeSnapshot();
        AnnounceRound();
    }
    FeedJerk();
    engine::SetTickMs(mReplay ? 1 : PacedTickMs()); // every frame: a restore brings back the game's tick length too (a replay: no waiting)
    SetNextInputs(local);
    if (mSynced)
    {
        SendCommands();
    }
}
