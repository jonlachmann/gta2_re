// Port/net (docs/netcode.md, "Architecture"): the client. It predicts its game ahead of the server's snapshots, sends
// its input, reconciles each snapshot (rolls back, applies it, runs the ticks since again) and shows the result.
#pragma once
#include "../core/protocol.hpp"
#include "../engine/engine.hpp"
#include "../transport/transport.hpp"
#include "../dev/client_probe.hpp"
#include "bot.hpp"
#include "recording.hpp"
#include "round_flow.hpp"
#include "session.hpp"
#include <memory>
#include <string>
#include <vector>

namespace net
{
struct ClientConfig
{
    std::string host = "127.0.0.1";
    uint16_t port = kDefaultPort;
    std::string name = "player";
    std::string bot; // a bot brain plays instead of the controls ("simple"); empty: a person
    int lagMs = 0, jitterMs = 0, lossPercent = 0; // the link conditioner (tests)
};

// Corrections of what is shown, for the log
struct CorrectionStats
{
    uint32_t count = 0, jumps = 0;
    float sum = 0, max = 0;
    void Add(float blocks, bool jumped)
    {
        count++;
        sum += blocks;
        max = blocks > max ? blocks : max;
        jumps += jumped;
    }
    void Print(uint32_t frame, const char* what) const;
};

class Client
{
  public:
    // Connects and waits for the server's welcome (a few seconds at most): then the game to start is known
    bool Connect(const ClientConfig& config, engine::NetworkGame* pGame, std::string* error);
    // The same without waiting (the frontend's network page): PollConnect each frame, 1 welcomed, 0 not yet, -1 failed
    bool BeginConnect(const ClientConfig& config, std::string* error);
    int PollConnect(engine::NetworkGame* pGame, std::string* error);
    // No server: a recorded session (GTA2_NET_RECORD) played back frame for frame, as fast as the game runs
    bool StartReplay(const char* path, engine::NetworkGame* pGame, std::string* error);
    void Frame();
    void Leave(); // tells the server at once (a quit, a signal), not by its timeout seconds later
    const std::string& LeftWhy() const { return mLeftWhy; } // why we went back to the menu ("": the player left)
    void Idle(); // between frames: a pong timed as it arrives, the rest kept for the frame

  private:
    struct Parts
    {
        uint32_t tick = 0;
        int count = 0, have = 0;
        std::vector<std::vector<uint8_t>> pieces;
    };
    // What this client did and saw at a tick (by tick % kRing)
    struct TickRecord
    {
        uint32_t tick = 0, buttons = 0; // the input used for the tick (tick: 0 until there is one)
        uint32_t shownTick = 0; // the tick <predX>, <predY> and <shown> are for (0: none)
        float predX = 0, predY = 0; // where the local player was predicted to be
        std::vector<engine::ReplicaPos> shown; // where the mapped cars and peds were shown (the display error)
    };
    // For the log, every 300 frames
    struct Stats
    {
        CorrectionStats players, cars;
        uint32_t rollbacks = 0, resimTicks = 0;
        float maxError = 0; // the prediction error
        double shownErrorSum = 0;
        uint32_t shownErrorCount = 0;
        float shownErrorMax = 0;
        uint32_t carFrames = 0, carMismatch = 0; // frames x players, and those where in-car differs from the snapshot
    };
    void Step(); // the frame's work: what arrived, the input, the snapshot, the commands
    void Welcomed(const Welcome& welcome, engine::NetworkGame* pGame);
    bool OnWelcome(const TransportEvent& e, engine::NetworkGame* pGame, std::string* error);
    void OnSnapshot(BitReader& r);
    void OnSnapshotPart(BitReader& r);
    bool TakeEvents(std::vector<TransportEvent>& events, uint32_t* pRecordedInput);
    uint32_t ChooseInput(uint32_t recordedInput);
    void StartBot();
    void CountTestStats();
    void RecordTick();
    void HandleEvents(const std::vector<TransportEvent>& events);
    void TakeSnapshot();
    void AnnounceRound();
    void OnMapChange(BitReader& r);
    void StartOver(); // a new game (the next map): synced again from its first snapshot
    void Resync(const Snapshot& snap);
    void Reconcile(const Snapshot& snap);
    void MeasureShownError(const Snapshot& snap);
    void TakePlayerStates(const Snapshot& snap);
    void Blend(const std::vector<engine::Pose>& shown);
    void PrintStats(const Snapshot& snap);
    int PacedTickMs() const;
    void SetNextInputs(uint32_t local);
    void SendCommands();
    uint32_t Lead() const; // ticks to run ahead of the newest snapshot
    void Reconnect(); // the connection dropped: a new one, now and then, until the server has us back
    void Rejoined(const Welcome& welcome);
    void LeaveToMenu(const char* why); // off the server, and the game back to the frontend's network page
    void ShowStatus(); // the ping, and an outage, over the frame
    // The round flow (session/round_flow.cpp), a client's part: its commands to the server, its keys, the panel
    void SendCommand(const std::string& text);
    void RoundKeys(uint32_t& local, uint32_t key); // F1 ready in the warmup, Y / N on an open vote
    void CompleteChat(uint32_t& local, uint32_t key); // Tab in a command line: completed (CompleteCommand)
    void WatchChat(); // a chat line typed to its end that starts with '/' goes as a command
    void ShowRound(); // the warmup, countdown, results and an open vote over the frame
    void BotRound(); // a bot readies up and votes
    bool TakePong(const TransportEvent& e); // a pong (the ping's round trip measured): not for the frame
    void SendPing();

    ClientConfig mConfig;
    std::unique_ptr<Transport> mTransport;
    bool mConnected = false;
    int mSlot = 0;
    int mTickMs = 33;
    uint32_t mFrame = 0;

    // Snapshots by tick % kRing: the baselines
    Snapshot mReceived[kRing];
    Parts mParts[4]; // snapshots coming in parts, by tick % 4
    uint32_t mLatestTick = 0;
    int mSlack = 0; // how many ticks early our commands reach the server (newest command - next tick)
    bool mSlackKnown = false;
    uint32_t mJumpTick = 0; // our tick at the last jump
    bool mNewSnapshot = false;

    // Prediction: this game's tick (the server tick it is predicting) and the game saved after each tick
    bool mSynced = false;
    uint32_t mTick = 0;
    std::vector<std::unique_ptr<engine::SavedWorld>> mSaves;
    TickRecord mHist[kRing];
    std::vector<uint32_t> mRemote; // other players' last inputs, from snapshots

    std::unique_ptr<BotBrain> mBot;
    std::unique_ptr<ClientRecorder> mRecorder;
    std::unique_ptr<ClientReplay> mReplay;
    uint32_t mRtt = 0; // the round trip as the transport sees it (or the recording says)
    uint32_t mPingMs = 0; // the network's round trip: a ping to its pong, both answered between frames (a link conditioner's delay too)
    uint32_t mPingShown = 0;
    long long mPingShownMs = 0, mLastSnapshotMs = 0, mReconnectMs = 0, mPingSentMs = 0;
    std::vector<TransportEvent> mPending; // what Idle polled, for the next frame
    int mReconnects = 0; // attempts since the connection dropped
    long long mLostMs = 0; // when it dropped
    bool mHelloSent = false; // joining: connected, waiting for the welcome
    long long mJoinWaitMs = 0; // joining: since when we wait (6 s each for the connection and the welcome)
    std::string mScr; // the map's script (a rejoin must find the same)
    std::unique_ptr<engine::NavMap> mNavMap; // built once the game has its map
    BotView mBotView;
    int mMarks = 0; // mark key presses (F12 or the key under Esc) (ChooseInput)
    engine::Round mRound; // the last one announced
    int mGeneration = 0; // engine::GameGeneration's, of the game running
    int mServerGame = -1; // the server's game we take snapshots of: the next after a map change (-1: the first one's)
    uint32_t mMarkFrame = 0;
    bool mLeavePrompt = false, mLeft = false; // Escape's prompt (ChooseInput), and its yes: left the server
    std::vector<std::string> mMaps; // what a map vote chooses from (the welcome's)
    struct ScoreRow
    {
        int slot, frags, deaths;
    };
    std::vector<ScoreRow> mResultsRows; // the scores as the results began
    bool mWasChatting = false;
    uint32_t mRoundKeyFrame = 0, mBotSentFrame = 0;
    std::string mLeftWhy;
    uint32_t mPromptKeyFrame = 0;
    std::vector<bool> mWasWasted; // the test log: deaths this game carried out per player
    std::vector<int> mLocalDeaths;
    Stats mStats;
    ClientProbe mProbe; // the checks and test logs (Port/net/dev)
};
} // namespace net
