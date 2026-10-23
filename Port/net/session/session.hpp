// Port/net (docs/netcode.md, "Architecture"): the server and the client, on top of core, transport and the
// engine interface. Both run inside the game's network frame (engine::SetFrameHook): after a tick, before drawing.
#pragma once
#include "../core/protocol.hpp"
#include "../core/snapshot.hpp"
#include "../engine/engine.hpp"
#include "../transport/transport.hpp"
#include "bot.hpp"
#include <memory>
#include <string>
#include <vector>

namespace net
{
struct ServerConfig
{
    uint16_t port = kDefaultPort;
    engine::NetworkGame game; // game.players: the player slots
    float interestRadius = 24.0f; // blocks around a client's player that its snapshots cover (players: always)
    int lagMs = 0, jitterMs = 0, lossPercent = 0; // the link conditioner (tests)
};

// "0:3/1 1:0/2": per player, frags/deaths (health while alive)
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
        uint32_t cmdTick[64] = {}; // received commands by tick % 64
        uint32_t cmdButtons[64] = {};
        uint32_t buttons = 0; // the input applied last (repeated while commands are late)
        uint32_t late = 0; // ticks without their command (counted for the log)
        uint32_t ackTick = 0; // the newest snapshot the client has
        Snapshot sent[64]; // what was sent, by tick % 64: the baselines
    };
    void OnReceive(const TransportEvent& e);
    void OnHello(uint32_t peer, BitReader& r);
    void SendSnapshot(int slot, Slot& s, const Snapshot& world);
    int SlotOfPeer(uint32_t peer) const;

    ServerConfig mConfig;
    std::unique_ptr<Transport> mTransport;
    std::vector<Slot> mSlots;
    uint32_t mTick = 0;
    uint32_t mBytesSent = 0;
    bool mStarted = false;
};

struct ClientConfig
{
    std::string host = "127.0.0.1";
    uint16_t port = kDefaultPort;
    std::string name = "player";
    std::string bot; // a bot brain plays instead of the controls ("simple"); empty: a person
    int lagMs = 0, jitterMs = 0, lossPercent = 0; // the link conditioner (tests)
};

class Client
{
  public:
    Client();
    ~Client();
    // Connects and waits for the server's welcome (a few seconds at most): then the game to start is known
    bool Connect(const ClientConfig& config, engine::NetworkGame* pGame, std::string* error);
    void Frame();

  private:
    void OnSnapshot(BitReader& r);
    void Reconcile(const Snapshot& snap);
    void Extrapolate(const Snapshot& snap);
    uint32_t Lead() const; // ticks to run ahead of the newest snapshot

    ClientConfig mConfig;
    std::unique_ptr<Transport> mTransport;
    bool mConnected = false;
    int mSlot = 0;
    int mTickMs = 33;
    uint32_t mFrame = 0;

    // Snapshots by tick % 64: the baselines
    Snapshot mReceived[64];
    uint32_t mLatestTick = 0;
    int mSlack = 0; // how many ticks early our commands reach the server (newest command - next tick)
    bool mSlackKnown = false;
    uint32_t mJumpTick = 0; // our tick at the last jump
    bool mNewSnapshot = false;

    // Prediction: this game's tick (the server tick it is predicting), the game saved after each tick, the inputs
    // used for each tick, and where the local player was predicted to be
    bool mSynced = false;
    uint32_t mTick = 0;
    std::vector<std::unique_ptr<engine::SavedWorld>> mSaves;
    uint32_t mHistTick[64] = {};
    uint32_t mHistButtons[64] = {};
    float mPredX[64] = {}, mPredY[64] = {};
    std::vector<uint32_t> mRemote; // other players' last inputs, from snapshots
    std::vector<engine::ReplicaPos> mShown[64]; // where each tick showed the mapped cars and peds (the display error)
    double mShownErrorSum = 0;
    uint32_t mShownErrorCount = 0;
    float mShownErrorMax = 0;
    // For the log
    uint32_t mRollbacks = 0, mResimTicks = 0;
    float mMaxError = 0;

    std::unique_ptr<BotBrain> mBot;
    std::unique_ptr<engine::NavMap> mNavMap; // built once the game has its map
    BotView mBotView;
    std::vector<bool> mWasWasted; // the test log: deaths this game carried out per player
    std::vector<int> mLocalDeaths;
};
} // namespace net
