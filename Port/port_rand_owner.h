// Port (docs/netcode.md, "Random draws"): who a random draw belongs to. In a network game an entity's update (a ped's,
// with its char, or a car's) and each service only the server runs (traffic, ambient peds, scripts, police...) draw
// their own numbers: a hash of the game's seed, the owner, the tick and how many it drew this update
// (Port/port_rand.cpp), not the game's one stream. A client simulates only its own area: with the one stream, every
// draw for an entity it doesn't have moved every later draw it makes, so its peds' random turns and choices were
// another's from the first tick. Owned, an entity decides the same way wherever it runs, whatever else exists.
// Draws without an owner (none here, or not a network game) stay the game's stream, as in 10.5.exe.
// The Source/ files that use this include it under #if defined(GTA2_PORT).
#pragma once

class Car;
class Ped;

// The services' owners (each its own stream)
enum PortRandService
{
    kPortRandTraffic = 0x7F000001, // TrafficSpawner::GenerateTraffic_583670
    kPortRandDummies, // PedManager::Dummies_470330
    kPortRandGenerators,
    kPortRandGarages,
    kPortRandScripts,
    kPortRandEmergency,
    kPortRandAmbulance,
    kPortRandPolice,
    kPortRandFireEngines,
};

// Set by the netcode: the owner key of a ped or a car, the server's net id (0: none, the game's stream)
extern unsigned int (*gPortRandPedKey)(const Ped* p);
extern unsigned int (*gPortRandCarKey)(const Car* c);
extern bool gPortRandOwned; // a network game: services own their draws too

// The owner for the scope's lifetime (the one before it after)
class PortRandScope
{
  public:
    explicit PortRandScope(unsigned int serviceKey);
    explicit PortRandScope(const Ped* p);
    explicit PortRandScope(const Car* c);
    template <class T> explicit PortRandScope(const T*) : mActive(false), mSavedKey(0), mSavedDraws(0)
    {
    } // other pools: whoever owns the draws already
    ~PortRandScope();

  private:
    void Enter(unsigned int key);
    bool mActive;
    unsigned int mSavedKey, mSavedDraws;
};

// The tick that starts (Game::UpdateGame_4B9410, the game's cycle): the owners' draws are numbered per tick
void PortRandTickStart(unsigned int cycle);
extern void (*gPortRandTickHook)(); // the netcode's, at each tick's start (a client's id maps for the keys)
void PortRandSetSeed(unsigned int seed); // the game's seed for the owners' draws (sent to clients with the world)
unsigned int PortRandSeed();
