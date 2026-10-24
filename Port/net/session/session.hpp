// Port/net (docs/netcode.md, "Architecture"): the server and the client, on top of core, transport and the
// engine interface. Both run inside the game's network frame (engine::SetFrameHook): after a tick, before drawing.
#pragma once
#include "../core/protocol.hpp"
#include "../core/snapshot.hpp"
#include "../engine/engine.hpp"
#include "../transport/transport.hpp"
#include "bot.hpp"
#include "jerk.hpp"
#include "replay.hpp"
#include <memory>
#include <string>
#include <vector>

namespace net
{
const int kRing = 64; // ticks of snapshots, commands and baselines kept (by tick % kRing)

// The session's GTA2_NET_* switches (docs/netcode.md), read once
struct SessionEnv
{
    const char* record; // a file to record the client's session to
    bool marker; // the server's position of the local player drawn
    bool corrLog, animLog, inputLog;
    int jerkLog; // 1: the jerk statistics, 2: and each jump
    float blend; // how far a correction is blended in each frame (0.2; 1: at once)
    const char* navDump; // the bots' map as a picture
    const char* testKill; // <tick>:<victim>:<killer>[,...]: server test kills
    const char* testArm; // <weapon>:<ammo>: every player kept armed on the server
};
const SessionEnv& Env();

struct ServerConfig
{
    uint16_t port = kDefaultPort;
    engine::NetworkGame game; // game.players: the player slots
    float interestRadius = 24.0f; // blocks around a client's player that its snapshots cover (players: always)
    int packets = 4; // a snapshot's packets at most (--packets, 1..8): a busy area's goes in parts
    int lagMs = 0, jitterMs = 0, lossPercent = 0; // the link conditioner (tests)
    std::vector<std::string> arena; // bot brains playing the first slots on the server itself (--arena)
    uint32_t endTick = 0; // the arena's result printed and the server ended at this tick (--ticks; 0: never)
    uint32_t seed = 0; // the game's random state at the start (--seed; 0: the game's own)
};

// The server's own bot players (arena.cpp), slots 0..n-1: a brain each, or a trainer's ("remote:<port>")
class Arena
{
  public:
    bool Start(const std::vector<std::string>& brains, std::string* error);
    int Count() const
    {
        return (int)mBots.size();
    }
    void Think(const Snapshot& world, uint32_t tick); // each bot's buttons for the next tick
    uint32_t Buttons(int slot) const;
    // "net: arena <slot> <brain> frags F deaths D near N dealt H cars C pickups P", and the trainer's last message
    void Finish(const Snapshot& world);

  private:
    struct Bot
    {
        struct Body // a tick's state, as Score reads it
        {
            float x = 0, y = 0;
            int rot = 0, health = 0, frags = 0, deaths = 0, guns = 0; // guns: the weapons with ammo
            uint32_t car = 0;
        };
        std::string kind;
        std::unique_ptr<BotBrain> brain; // NULL: remote
        uint32_t buttons = 0;
        Body last;
        double closeness = 0; // summed per tick: 1 - distance / 50 blocks to the nearest other player
        int dealt = 0, cars = 0, pickups = 0;
        float reward = 0; // since the last message (remote)
        BotMemory memory; // its features' (remote)
    };
    void Score(const Snapshot& world);
    void AddRemote(int slot, std::vector<float>& message);
    bool Exchange(const std::vector<float>& message, bool done);

    std::vector<Bot> mBots;
    std::unique_ptr<engine::NavMap> mNavMap;
    BotView mView;
    uint32_t mTicks = 0;
    int mSocket = -1; // to the trainer
};

// " p:f/d(h)@x,y": per player, frags/deaths (health) and where
std::string Scoreboard(const Snapshot& snap);

class Server
{
  public:
    bool Start(const ServerConfig& config, std::string* error);
    void Frame();

  private:
    struct Slot
    {
        bool used = false;
        uint32_t peer = 0;
        std::string name;
        uint32_t lastCmdTick = 0; // the newest command tick received (late ones too)
        uint32_t cmdTick[kRing] = {}; // received commands by tick % kRing
        uint32_t cmdButtons[kRing] = {};
        uint32_t buttons = 0; // the input applied last (repeated while commands are late)
        uint32_t late = 0; // ticks without their command (counted for the log)
        uint32_t ackTick = 0; // the newest snapshot the client has
        Snapshot sent[kRing]; // what was sent, by tick % kRing: the baselines
    };
    // For the log, every 300 ticks
    struct Stats
    {
        uint32_t bytes = 0, parts = 0;
        uint32_t inArea = 0, fresh = 0, cut = 0; // entities in the areas, with new state, snapshots without it for all
        uint32_t unsent = 0; // snapshots that fit no budget
    };
    // A nearby entity, for filling a snapshot by priority
    struct Item
    {
        const EntityState* now;
        const EntityState* had; // in the client's baseline
        EntityState aged; // what it has, with its age
        size_t freshBits, agedBits;
        float priority;
        int pick; // 0: not sent (new, no room), 1: aged, 2: this tick's
    };
    void OnReceive(const TransportEvent& e);
    void OnHello(uint32_t peer, BitReader& r);
    void OnCommands(int slot, BitReader& r);
    void StartGameOnce();
    void PollTransport();
    void RunTestHooks();
    void SetNextInputs(const Snapshot& world);
    void FeedJerk(const Snapshot& world);
    void PrintStats(const Snapshot& world);
    void SendSnapshot(int slot, Slot& s, const Snapshot& world);
    std::vector<Item> PriceArea(const std::vector<const EntityState*>& area, const Snapshot* base, float x, float y) const;
    bool Fill(Slot& s, const Snapshot& fixed, std::vector<Item>& items, const Snapshot* base, BitWriter& w);
    void Transmit(const Slot& s, const BitWriter& w);
    int SlotOfPeer(uint32_t peer) const;

    ServerConfig mConfig;
    std::unique_ptr<Transport> mTransport;
    std::vector<Slot> mSlots;
    uint32_t mTick = 0;
    bool mStarted = false;
    Stats mStats;
    JerkMeter mJerk;
    Arena mArena;
};

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
    // No server: a recorded session (GTA2_NET_RECORD) played back frame for frame, as fast as the game runs
    bool StartReplay(const char* path, engine::NetworkGame* pGame, std::string* error);
    void Frame();

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
    void Welcomed(const Welcome& welcome, engine::NetworkGame* pGame);
    void OnSnapshot(BitReader& r);
    void OnSnapshotPart(BitReader& r);
    bool TakeEvents(std::vector<TransportEvent>& events, uint32_t* pRecordedInput);
    uint32_t ChooseInput(uint32_t recordedInput);
    void StartBot();
    void CountTestStats();
    void RecordTick();
    void HandleEvents(const std::vector<TransportEvent>& events);
    void TakeSnapshot();
    void Resync(const Snapshot& snap);
    void Reconcile(const Snapshot& snap);
    void MeasureShownError(const Snapshot& snap);
    void TakePlayerStates(const Snapshot& snap);
    void Blend(const std::vector<engine::Pose>& shown);
    void LogCorrection(const engine::Pose& n, int32_t dx, int32_t dy, int32_t dr) const;
    void PrintStats(const Snapshot& snap);
    void FeedJerk();
    int PacedTickMs() const;
    void SetNextInputs(uint32_t local);
    void SendCommands();
    uint32_t Lead() const; // ticks to run ahead of the newest snapshot

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
    std::unique_ptr<engine::NavMap> mNavMap; // built once the game has its map
    BotView mBotView;
    uint32_t mLastInput = ~0u; // for the input log
    std::vector<bool> mWasWasted; // the test log: deaths this game carried out per player
    std::vector<int> mLocalDeaths;
    Stats mStats;
    JerkMeter mJerk;
};
} // namespace net
