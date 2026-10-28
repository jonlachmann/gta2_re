// Port/net (docs/netcode.md, "How it works"): the server. It runs the game, takes the clients' commands and sends each
// client snapshots of what is around it; it runs the round flow (round_flow.hpp).
#pragma once
#include "../core/protocol.hpp"
#include "../engine/engine.hpp"
#include "../transport/transport.hpp"
#include "../dev/arena.hpp"
#include "../dev/server_probe.hpp"
#include "round_flow.hpp"
#include "session.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace net
{
struct MapFiles // a multiplayer map, from its data/<name>.mmp
{
    std::string name, gmp, sty, scr;
};

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
    std::vector<MapFiles> maps; // played in turn, a round each (--maps); fewer than two: the rounds stay on the map
    RoundRules rules; // the round flow (session/round_flow.cpp): warmup, countdown, results, votes
    std::vector<MapFiles> voteMaps; // what a vote chooses from: the rotation, else every multiplayer map there is
    int currentMap = 0; // the first map's number in voteMaps
};

class Server
{
  public:
    bool Start(const ServerConfig& config, std::string* error);
    void Frame();
    void Idle(); // between frames: a ping answered at once, the rest kept for the frame

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
    void OnEvent(const TransportEvent& e);
    void SetNextInputs(const Snapshot& world);
    void RunRounds(const Snapshot& world); // the round flow's tick and what it asks for
    void OnCommand(int slot, BitReader& r);
    void Carry(const RoundActions& actions);
    void ChangeToMap(int map); // voteMaps' <map>, for everyone
    void Kick(int slot);
    void Notice(int slot, const std::string& text); // to one client (slot) or all (-1)
    std::vector<RoundPlayer> RoundPlayers(const Snapshot& world) const;
    void StartOver(); // a new game (the next map): its start, and every client's baseline, again
    void PrintStats(const Snapshot& world);
    void SendSnapshot(int slot, Slot& s, const Snapshot& world);
    std::vector<Item> PriceArea(const std::vector<const EntityState*>& area, const Snapshot* base, float x, float y) const;
    bool Fill(Slot& s, const Snapshot& fixed, std::vector<Item>& items, const Snapshot* base, BitWriter& w);
    void Transmit(const Slot& s, const BitWriter& w);
    int SlotOfPeer(uint32_t peer) const;

    ServerConfig mConfig;
    std::unique_ptr<Transport> mTransport;
    std::vector<Slot> mSlots;
    std::vector<TransportEvent> mPending; // what Idle polled, for the next frame
    uint32_t mTick = 0;
    bool mStarted = false;
    Stats mStats;
    ServerProbe mProbe; // the test hooks and logs (Port/net/dev)
    Arena mArena;
    RoundFlow mRounds;
    std::vector<RoundPlayer> mRoundPlayers; // as of the last frame, for commands between frames
    std::map<std::string, uint32_t> mBanned; // kicked names, until this tick
    size_t mMap = 0; // in mConfig.voteMaps
    int mGeneration = 0; // engine::GameGeneration's, of the game running
};
} // namespace net
