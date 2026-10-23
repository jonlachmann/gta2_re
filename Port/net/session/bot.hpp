// Port/net (docs/netcode.md, "Bots"): bots as players that perceive and decide on their own. A bot gets a view of
// what its client knows (its own body as predicted, the snapshot's players, cars and peds, the static map) and
// returns the input bits a person would press. It uses none of the game's logic (no route finder, no ped AI), so
// other brains can come in: smarter ones, or one that sends the view to another process and reads its answer.
#pragma once
#include "../engine/engine.hpp"
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

namespace net
{
// The game's input bits (Source/input_bits.hpp)
enum BotButton : uint32_t
{
    kBtnForward = 0x1,
    kBtnBack = 0x2,
    kBtnLeft = 0x4, // turns towards larger angles
    kBtnRight = 0x8,
    kBtnAttack = 0x10,
    kBtnEnterExit = 0x20,
    kBtnJump = 0x40, // handbrake in a car
    kBtnPrevWeapon = 0x80,
    kBtnNextWeapon = 0x100,
};

struct BotView
{
    uint32_t tick = 0;
    uint32_t frame = 0;
    int self = 0; // player index
    // Its own body: x, y, z in blocks, rot 0..1439 (walking while facing rot moves along (sin, cos): measured)
    bool hasBody = false;
    float x = 0, y = 0, z = 0;
    int rot = 0;
    int health = 0;
    struct Seen
    {
        enum Kind
        {
            kPlayer,
            kCar,
            kPed,
        } kind;
        uint32_t id;
        int player; // for kPlayer
        float x, y, z;
        int rot;
        int health; // for kPlayer
    };
    std::vector<Seen> seen; // what the client's snapshot has, others only
    const engine::NavMap* map = nullptr;
};

class BotBrain
{
  public:
    virtual ~BotBrain()
    {
    }
    virtual uint32_t Think(const BotView& view) = 0;
    virtual std::string Describe() const // a line for the client's log
    {
        return std::string();
    }
};

// "simple": walks to the nearest player along its own route and shoots when close and facing it
std::unique_ptr<BotBrain> CreateBotBrain(const std::string& kind);

// The view a client gives its bot (bot.cpp)
void FillBotView(BotView& view, const Snapshot& snap, int self, uint32_t frame, const engine::NavMap* map);

// Navigation (bot_nav.cpp, no game code): the cells (y * 256 + x) from start to goal, start excluded; false if
// there is no way within <maxExpand> cells looked at
bool FindPath(const engine::NavMap& map, int sx, int sy, int gx, int gy, std::vector<int>& path, int maxExpand = 40000);
} // namespace net
