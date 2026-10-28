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
const int kStallMs = 700; // no snapshot for this long: the connection is out (ENet drops it after 5 to 10 s)
const int kReconnectMs = 3000; // between attempts to connect again
const int kGiveUpMs = 30000; // reconnecting this long: the server is gone, back to the menu

long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
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
    mMaps = welcome.maps;
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
    mScr = welcome.scr;
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

bool net::Client::BeginConnect(const ClientConfig& config, std::string* error)
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
    mHelloSent = false;
    mJoinWaitMs = NowMs();
    return true;
}

// The connection, our hello, the server's welcome: 1 welcomed (the game to start is in <pGame>), 0 not yet, -1 failed
int net::Client::PollConnect(engine::NetworkGame* pGame, std::string* error)
{
    TransportEvent e;
    while (mTransport->Poll(e))
    {
        if (e.type == TransportEvent::kDisconnect)
        {
            *error = mHelloSent ? "the server closed the connection" : "no answer from " + mConfig.host;
            return -1;
        }
        if (e.type == TransportEvent::kConnect && !mHelloSent)
        {
            Hello hello;
            hello.name = mConfig.name;
            BitWriter w;
            WriteHello(w, hello);
            mTransport->Send(0, kChannelReliable, w.Data().data(), w.ByteCount(), true);
            mTransport->Flush();
            mHelloSent = true;
            mJoinWaitMs = NowMs(); // and as long again for the welcome
        }
        else if (e.type == TransportEvent::kReceive && mHelloSent && e.channel == kChannelReliable)
        {
            return OnWelcome(e, pGame, error) ? 1 : -1;
        }
    }
    if (NowMs() - mJoinWaitMs > 6000)
    {
        *error = mHelloSent ? "the server didn't welcome us" : "no answer from " + mConfig.host;
        return -1;
    }
    return 0;
}

bool net::Client::Connect(const ClientConfig& config, engine::NetworkGame* pGame, std::string* error)
{
    if (!BeginConnect(config, error))
    {
        return false;
    }
    int result = 0;
    while (!(result = PollConnect(pGame, error)))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return result > 0;
}

bool net::Client::OnWelcome(const TransportEvent& e, engine::NetworkGame* pGame, std::string* error)
{
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
            printf("net: recording to %s (F12 or the key under Esc marks a moment)\n", path.c_str());
        }
    }
    printf("net: joined %s:%u as player %d of %d, %s\n", mConfig.host.c_str(), mConfig.port, mSlot, welcome.players, welcome.scr.c_str());
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
    mLastSnapshotMs = NowMs();
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
    events.swap(mPending);
    TransportEvent e;
    while (mTransport && mTransport->Poll(e))
    {
        if (!TakePong(e))
        {
            events.push_back(e);
        }
    }
    mRtt = mTransport ? mTransport->RoundTripMs(0) : 0;
    return true;
}

// Between frames: polled every millisecond or so, so a pong is timed when it comes, not at the next frame
void net::Client::Idle()
{
    TransportEvent e;
    while (!mReplay && mTransport && mTransport->Poll(e))
    {
        if (!TakePong(e))
        {
            mPending.push_back(e);
        }
    }
}

bool net::Client::TakePong(const TransportEvent& e)
{
    if (e.type != TransportEvent::kReceive || e.channel != kChannelUnreliable || e.data.empty() || e.data[0] != kMsgPong)
    {
        return false;
    }
    BitReader r(e.data.data(), e.data.size());
    r.Bits(8);
    const uint32_t sent = r.Bits(32);
    const uint32_t sample = (uint32_t)NowMs() - sent;
    if (sample < 10000) // ponytail: a pong from before a reconnect or a clock wrap, dropped
    {
        mPingMs = mPingMs ? (mPingMs * 3 + sample) / 4 : sample;
    }
    return true;
}

// Twice a second: our clock to the server, which sends it straight back
void net::Client::SendPing()
{
    const long long now = NowMs();
    if (!mTransport || now - mPingSentMs < 500)
    {
        return;
    }
    mPingSentMs = now;
    BitWriter w;
    w.Bits(kMsgPing, 8);
    w.Bits((uint32_t)now, 32);
    mTransport->Send(0, kChannelUnreliable, w.Data().data(), w.ByteCount(), false);
    mTransport->Flush();
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
    // F12, or the key under Esc (` or §: keyboards without F12), keys the game doesn't use (here as their DirectInput
    // codes and the key-down bit: Source/input_bits.hpp), mark the moment, in play and in its replay alike
    const uint32_t kKeyDown = 0x200000, kMarkF12 = 0x58, kMarkGrave = 0x29; // DIK_F12, DIK_GRAVE
    const uint32_t key = (local >> 12) & 0x1FF;
    if ((local & kKeyDown) && (key == kMarkF12 || key == kMarkGrave))
    {
        if (mFrame != mMarkFrame + 1) // held: one mark
        {
            printf("net: mark %d at frame %u, tick %u\n", ++mMarks, mFrame, mTick);
        }
        mMarkFrame = mFrame;
    }
    // Escape, and Y or N while its prompt shows, are this client's: the prompt to leave, drawn over the frame (ShowStatus).
    // Neither the game here nor the server gets them: the game's own quit prompt is on its player, which the snapshots
    // and resimulations set again (it showed for a moment), and the server's yes ended everyone's game
    const uint32_t kKeyBits = 0x1FF000, kEscape = 0x01, kYes = 0x15, kNo = 0x31; // DIK_ESCAPE, DIK_Y, DIK_N
    if (!mReplay && (local & kKeyDown) && (key == kEscape || (mLeavePrompt && (key == kYes || key == kNo))))
    {
        if (mFrame != mPromptKeyFrame + 1) // held: once
        {
            if (key == kYes)
            {
                printf("net: frame %u, left the game\n", mFrame);
                mLeavePrompt = false;
                LeaveToMenu("");
            }
            else
            {
                mLeavePrompt = key == kEscape && !mLeavePrompt;
            }
        }
        mPromptKeyFrame = mFrame;
        local &= ~(kKeyDown | kKeyBits);
    }
    if (!mReplay && !mLeavePrompt && (local & kKeyDown))
    {
        CompleteChat(local, key);
        RoundKeys(local, key);
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
            const uint32_t type = r.Bits(8);
            Welcome welcome;
            Reject reject;
            if (type == kMsgMapChange)
            {
                OnMapChange(r);
            }
            else if (type == kMsgWelcome && !mConnected && ReadWelcome(r, welcome))
            {
                Rejoined(welcome);
            }
            else if (type == kMsgNotice)
            {
                std::string text;
                if (ReadText(r, text))
                {
                    engine::ShowHudMessage(text.c_str());
                    printf("net: frame %u, notice: %s\n", mFrame, text.c_str());
                }
            }
            else if (type == kMsgReject && mConnected && ReadReject(r, reject))
            {
                printf("net: frame %u, the server put us out: %s\n", mFrame, reject.reason.c_str());
                LeaveToMenu(reject.reason == "kicked by vote" ? "kicked by vote" : "the server put us out");
            }
            else if (type == kMsgReject && !mConnected && ReadReject(r, reject))
            {
                printf("net: frame %u, the server won't have us back: %s\n", mFrame, reject.reason.c_str());
                mTransport.reset(); // and try again: it may be full only for now
            }
        }
        else if (e.type == TransportEvent::kConnect && !mConnected && mTransport)
        {
            Hello hello;
            hello.name = mConfig.name; // the server gives us our slot back by it
            BitWriter w;
            WriteHello(w, hello);
            mTransport->Send(0, kChannelReliable, w.Data().data(), w.ByteCount(), true);
            mTransport->Flush();
        }
        else if (e.type == TransportEvent::kDisconnect)
        {
            if (mConnected)
            {
                printf("net: frame %u, lost the server\n", mFrame);
                mReconnects = 0;
                std::fill(mRemote.begin(), mRemote.end(), 0u); // the others stand still meanwhile
            }
            mConnected = false;
            mTransport.reset();
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
        mScr = m.scr;
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

// When time is short, on the HUD (who won is the server's notice)
void net::Client::AnnounceRound()
{
    const engine::Round& now = engine::CurrentRound();
    std::string line;
    if (now.number == mRound.number && now.secondsLeft < mRound.secondsLeft && (now.secondsLeft == 60 || now.secondsLeft == 10))
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
    if (!mReplay) // a replay has no network
    {
        printf("net: frame %u, ping %u ms (ENet's %u)\n", mFrame, mPingMs, mRtt);
    }
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

void net::Client::Reconnect()
{
    const long long now = NowMs();
    mLostMs = mReconnects ? mLostMs : now;
    if (now - mLostMs > kGiveUpMs)
    {
        printf("net: frame %u, no answer from the server after %d attempts\n", mFrame, mReconnects);
        LeaveToMenu("lost the server");
        return;
    }
    if (mTransport || now < mReconnectMs)
    {
        return; // connecting (the hello follows its kConnect: HandleEvents), or not yet
    }
    mReconnectMs = now + kReconnectMs;
    mReconnects++;
    printf("net: frame %u, connecting to %s:%u again (attempt %d)\n", mFrame, mConfig.host.c_str(), mConfig.port, mReconnects);
    std::string error;
    mTransport.reset(CreateClientTransport(mConfig.host.c_str(), mConfig.port, &error));
    if (mTransport && (mConfig.lagMs || mConfig.jitterMs || mConfig.lossPercent))
    {
        mTransport.reset(CreateConditioner(mTransport.release(), mConfig.lagMs, mConfig.jitterMs, mConfig.lossPercent));
    }
}

// Back in: the same game goes on from the server's next snapshot, if the server has us as the same player on the same
// map (a server restarted on it too: its ticks start over)
void net::Client::Rejoined(const Welcome& welcome)
{
    if (welcome.version != kProtocolVersion || welcome.slot != mSlot || welcome.scr != mScr)
    {
        printf("net: frame %u, the server has us back as player %d on %s, not %d on %s: start the game again to play on\n", mFrame,
               welcome.slot, welcome.scr.c_str(), mSlot, mScr.c_str());
        LeaveToMenu("the server started another game");
        return;
    }
    printf("net: frame %u, back in as player %d\n", mFrame, mSlot);
    mConnected = true;
    mSynced = false;
    mNewSnapshot = false;
    mLatestTick = 0;
    mServerGame = -1; // a restarted server counts its games from the start again
    mLastSnapshotMs = NowMs();
    for (Snapshot& s : mReceived)
    {
        s = Snapshot();
    }
    for (Parts& p : mParts)
    {
        p = Parts();
    }
}

void net::Client::ShowStatus()
{
    const long long now = NowMs();
    if (now - mPingShownMs >= 1000) // a number that holds still long enough to read
    {
        mPingShown = mPingMs;
        mPingShownMs = now;
    }
    std::string centre;
    if (mLeft)
    {
        centre = "";
    }
    else if (mLeavePrompt)
    {
        centre = "Leave the game?  Y / N";
    }
    else if (!mConnected)
    {
        centre = "Lost the server, reconnecting";
    }
    else if (mSynced && now - mLastSnapshotMs > kStallMs)
    {
        centre = "Connection interrupted";
    }
    engine::SetNetStatus(mConnected && mPingShown ? "ping " + std::to_string(mPingShown) : "", centre);
}

void net::Client::Leave()
{
    mTransport.reset(); // its destructor disconnects (enet_peer_disconnect_now)
}

void net::Client::LeaveToMenu(const char* why)
{
    mLeft = true; // no reconnecting
    mConnected = false;
    mLeftWhy = why;
    Leave();
    engine::LeaveForMenu(); // and the frontend's network page (Port/net/net_main.cpp, LeaveNetworkGame)
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
        _Exit(0); // not exit: its destructors ran under the audio and GL threads still going (freed memory read on Linux)
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
    if (!mReplay)
    {
        ShowStatus();
        ShowRound();
        WatchChat();
        BotRound();
    }
    if (!mConnected)
    {
        if (!mReplay && !mLeft)
        {
            Reconnect();
        }
        SetNextInputs(local); // the game goes on here meanwhile: ours, the others' standing still
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
    if (!mReplay)
    {
        SendPing();
    }
}

// --- The round flow, a client's part (docs/netcode.md, "Round flow") ---

void net::Client::SendCommand(const std::string& text)
{
    if (mReplay || !mTransport || !mConnected)
    {
        return;
    }
    BitWriter w;
    WriteText(w, kMsgCommand, text);
    mTransport->Send(0, kChannelReliable, w.Data().data(), w.ByteCount(), true);
    printf("net: frame %u, command %s\n", mFrame, text.c_str());
}

// F1 in the warmup (ready), Y or N while a vote is open; not while typing a chat line, whose keys they are then. The
// keys go to neither the game here nor the server
void net::Client::RoundKeys(uint32_t& local, uint32_t key)
{
    const uint32_t kKeyDown = 0x200000, kKeyBits = 0x1FF000, kF1 = 0x3B, kYes = 0x15, kNo = 0x31; // DIK_F1, DIK_Y, DIK_N
    const engine::Round& round = engine::CurrentRound();
    const bool ready = key == kF1 && round.phase == kPhaseWarmup && round.minPlayers;
    const bool vote = (key == kYes || key == kNo) && round.voteKind != kVoteNone;
    if ((!ready && !vote) || engine::TakeLocalChat().chatting)
    {
        return;
    }
    if (mFrame != mRoundKeyFrame + 1) // held: once
    {
        SendCommand(ready ? "/ready" : key == kYes ? "/yes" : "/no");
    }
    mRoundKeyFrame = mFrame;
    local &= ~(kKeyDown | kKeyBits);
}

// Tab in a chat line starting with '/': its last word completed (CompleteCommand), the options over the frame when
// several fit. The key goes to neither the game here nor the server; the others see the line as the server typed it
void net::Client::CompleteChat(uint32_t& local, uint32_t key)
{
    const uint32_t kKeyDown = 0x200000, kKeyBits = 0x1FF000, kTab = 0x0F; // DIK_TAB
    engine::LocalChat chat = engine::TakeLocalChat();
    if (key != kTab || !chat.chatting)
    {
        return;
    }
    local &= ~(kKeyDown | kKeyBits);
    std::string line;
    for (int i = 0; i < chat.length && i < 79; i++)
    {
        line += (char)(chat.text[i] & 0xFF);
    }
    std::vector<std::string> names, options;
    for (int i = 0; i < engine::PlayerCount(); i++)
    {
        if (engine::SlotInGame(i))
        {
            names.push_back(engine::PlayerName(i));
        }
    }
    const std::string done = CompleteCommand(line, mMaps, names, options);
    chat.length = (int)(done.size() < 78 ? done.size() : 78);
    for (int i = 0; i < 79; i++)
    {
        chat.text[i] = i < chat.length ? (short)(unsigned char)done[i] : 0;
    }
    engine::PutLocalChat(chat);
    if (!options.empty())
    {
        std::string list;
        for (const std::string& o : options)
        {
            list += (list.empty() ? "" : ",  ") + o;
        }
        engine::ShowHudMessage(list.c_str());
    }
}

void net::Client::WatchChat()
{
    const engine::LocalChat chat = engine::TakeLocalChat();
    if (mWasChatting && !chat.chatting)
    {
        std::string line;
        for (int i = 0; i < chat.length && i < 79; i++)
        {
            line += (char)(chat.text[i] & 0x7F);
        }
        if (!line.empty() && line[0] == '/')
        {
            SendCommand(line);
        }
    }
    mWasChatting = chat.chatting != 0;
}

void net::Client::ShowRound()
{
    const engine::Round& r = engine::CurrentRound();
    const Snapshot& snap = mReceived[mLatestTick % kRing];
    std::vector<ScoreRow> rows;
    for (const EntityState& e : snap.entities)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(e, &p, &b))
        {
            rows.push_back(ScoreRow{p, engine::PlayerFrags(e), engine::PlayerDeaths(e)});
        }
    }
    // The results show the scores the round ended with: the game goes on meanwhile, and so do the frags
    if (r.phase == kPhaseResults && mResultsRows.empty())
    {
        mResultsRows = rows;
    }
    else if (r.phase != kPhaseResults)
    {
        mResultsRows.clear();
    }
    auto bits = [](unsigned v) {
        int n = 0;
        for (; v; v &= v - 1)
        {
            n++;
        }
        return n;
    };
    std::vector<std::string> lines;
    if (!mConnected || mLeft)
    {
        // nothing: the connection's own lines show
    }
    else if (r.phase == kPhaseWarmup)
    {
        int ready = 0;
        for (const ScoreRow& row : rows)
        {
            ready += row.slot < kRoundSlots && (r.ready >> row.slot & 1);
        }
        lines.push_back("Warmup");
        if ((int)rows.size() < r.minPlayers)
        {
            lines.push_back("Waiting for players: " + std::to_string(rows.size()) + " of " + std::to_string(r.minPlayers));
        }
        else
        {
            lines.push_back(std::to_string(ready) + " of " + std::to_string(rows.size()) + " ready" +
                            (r.phaseLeft ? ", the round starts in " + std::to_string(r.phaseLeft) + " s anyway" : ""));
        }
        lines.push_back(mSlot < kRoundSlots && (r.ready >> mSlot & 1) ? "You are ready" : "Press F1 when ready");
    }
    else if (r.phase == kPhaseCountdown)
    {
        lines.push_back("Round " + std::to_string(r.number) + " starts in " + std::to_string(r.phaseLeft));
    }
    else if (r.phase == kPhaseResults)
    {
        lines.push_back("Round " + std::to_string(r.number - 1) + ": " + (r.winner ? engine::PlayerName(r.winner - 1) + " wins" : std::string("a draw")));
        std::vector<ScoreRow> board = mResultsRows;
        std::sort(board.begin(), board.end(), [](const ScoreRow& a, const ScoreRow& b) { return a.frags > b.frags; });
        for (const ScoreRow& row : board)
        {
            lines.push_back(engine::PlayerName(row.slot) + ":  " + std::to_string(row.frags) + (row.frags == 1 ? " frag,  " : " frags,  ") +
                            std::to_string(row.deaths) + (row.deaths == 1 ? " death" : " deaths"));
        }
        lines.push_back("");
        std::string maps;
        for (size_t m = 0; m < mMaps.size(); m++)
        {
            int votes = 0;
            for (int i = 0; i < kRoundSlots; i++)
            {
                votes += r.mapChoice[i] == m + 1;
            }
            maps += (m ? "   " : "") + std::to_string(m + 1) + " " + mMaps[m] + (votes ? " (" + std::to_string(votes) + ")" : "");
            if (m % 3 == 2 || m + 1 == mMaps.size())
            {
                lines.push_back(maps);
                maps.clear();
            }
        }
        lines.push_back("Next map: type /map N (F10 to chat).  Next in " + std::to_string(r.phaseLeft) + " s");
    }
    if (r.voteKind != kVoteNone && mConnected && !mLeft)
    {
        const std::string what = r.voteKind == kVoteStart ? "start the round"
                                 : r.voteKind == kVoteMap ? "change to " + (r.voteArg < mMaps.size() ? mMaps[r.voteArg] : std::string("a map"))
                                                          : "kick " + engine::PlayerName(r.voteArg);
        const bool mine = mSlot < kRoundSlots && ((r.voteYes | r.voteNo) >> mSlot & 1);
        lines.push_back("");
        lines.push_back("Vote: " + what + "   " + std::to_string(bits(r.voteYes)) + " yes, " + std::to_string(bits(r.voteNo)) + " no   " +
                        std::to_string(r.voteLeft) + " s" + (mine ? "" : "   Y / N"));
    }
    engine::SetNetPanel(lines);
}

void net::Client::BotRound()
{
    // GTA2_NET_SAY=<frame>:<command>[;...]: test commands, at their frame
    for (const char* k = Env().say; k && *k; k = strchr(k, ';'), k = k ? k + 1 : NULL)
    {
        const unsigned frame = (unsigned)atoi(k);
        const char* colon = strchr(k, ':');
        const char* end = strchr(k, ';');
        if (colon && frame == mFrame && mConnected)
        {
            SendCommand(std::string(colon + 1, end ? end : colon + 1 + strlen(colon + 1)));
        }
    }
    if (!mBot || !mConnected || mFrame < mBotSentFrame + 90)
    {
        return;
    }
    const engine::Round& r = engine::CurrentRound();
    const bool ready = mSlot < kRoundSlots && (r.ready >> mSlot & 1);
    if (r.phase == kPhaseWarmup && r.minPlayers && !ready)
    {
        SendCommand("/ready");
    }
    else if (r.phase == kPhaseResults && !mMaps.empty() && mSlot < kRoundSlots && !r.mapChoice[mSlot])
    {
        SendCommand(mMaps.size() > 1 ? "/map 2" : "/map 1"); // the same choice for every bot: a test sees the map change
    }
    else if (r.voteKind != kVoteNone && mSlot < kRoundSlots && !((r.voteYes | r.voteNo) >> mSlot & 1))
    {
        SendCommand(r.voteKind == kVoteKick && r.voteArg == mSlot ? "/no" : "/yes"); // anything but its own kick
    }
    else
    {
        return;
    }
    mBotSentFrame = mFrame;
}

