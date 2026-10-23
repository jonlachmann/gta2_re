// Port/net (docs/netcode.md, "Architecture"): the server and the client, on top of core, transport and the
// engine interface. Both run inside the game's network frame (engine::SetFrameHook): after a tick, before drawing.
#pragma once
#include "../core/protocol.hpp"
#include "../core/snapshot.hpp"
#include "../engine/engine.hpp"
#include "../transport/transport.hpp"
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
};

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
        uint32_t lastCmdTick = 0; // the newest command tick received
        uint32_t buttons = 0; // the newest command's input
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
};

struct ClientConfig
{
    std::string host = "127.0.0.1";
    uint16_t port = kDefaultPort;
    std::string name = "player";
    bool bot = false; // input from a simple bot instead of the controls
};

class Client
{
  public:
    // Connects and waits for the server's welcome (a few seconds at most): then the game to start is known
    bool Connect(const ClientConfig& config, engine::NetworkGame* pGame, std::string* error);
    void Frame();

  private:
    void OnSnapshot(BitReader& r);
    uint32_t BotButtons();

    ClientConfig mConfig;
    std::unique_ptr<Transport> mTransport;
    bool mConnected = false;
    int mSlot = 0;
    uint32_t mFrame = 0;
    Snapshot mReceived[64]; // by tick % 64: the baselines
    uint32_t mLatestTick = 0;
    bool mNewSnapshot = false;
    std::vector<InputCmd> mCmds; // the last few sent, to repeat
    uint32_t mBotSeed = 1;
    uint32_t mBotButtons = 0;
};
} // namespace net
