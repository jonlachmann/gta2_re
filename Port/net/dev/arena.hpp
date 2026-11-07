// Port/net dev (docs/netcode.md, "Bots"): an arena, bot players on the server itself (--arena): matches for tests and
// the bots' training (a trainer's socket: Scripts/port/bots/rl_bots.py).
#pragma once
#include "../core/snapshot.hpp"
#include "../engine/engine.hpp"
#include "../session/bot.hpp"
#include <memory>
#include <string>
#include <vector>

namespace net
{
// The server's own bot players (arena.cpp), slots 0..n-1: a brain each, or a trainer's ("remote:<port>[:<teacher>]")
class Arena
{
  public:
    bool Start(const std::vector<std::string>& brains, std::string* error);
    int Count() const
    {
        return (int)mBots.size();
    }
    void NewMap()
    {
        mNavMap.reset(); // made again for the next one
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
            float nearest = 0; // blocks to the nearest other player alive
            uint32_t car = 0;
        };
        std::string kind;
        std::unique_ptr<BotBrain> brain; // NULL: remote
        uint32_t buttons = 0;
        Body last;
        double closeness = 0; // summed per tick: 1 - distance / 50 blocks to the nearest other player
        int dealt = 0, cars = 0, pickups = 0;
        float reward = 0; // since the last message (remote)
        bool droveThisLife = false; // (remote) the car reward is paid once a life
        BotMemory memory; // its features' (remote)
        std::unique_ptr<BotBrain> teacher; // a scripted brain on the same view (remote): what it would press
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
} // namespace net
