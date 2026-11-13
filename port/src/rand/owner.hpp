// Who a random draw belongs to (port/docs/netcode.md, "Random draws"). In a network game an entity's update (a ped's,
// with its char, or a car's) and each service only the server runs (traffic, ambient peds, scripts, police...) draw
// their own numbers: a hash of the game's seed, the owner, the tick and how many it drew this update
// (port/src/rand/rand.cpp), not the game's one stream. A client simulates only its own area: with the one stream, every
// draw for an entity it doesn't have moved every later draw it makes, so its peds' random turns and choices were
// another's from the first tick. Owned, an entity decides the same way wherever it runs, whatever else exists.
// Draws without an owner (none here, or not a network game) stay the game's stream, as in 10.5.exe.
// The Source/ files that use this include it under #if defined(GTA2_PORT).
#pragma once

class Car;
class Ped;

namespace PortRand
{
// The services' owners (each its own stream)
enum Service
{
    kTraffic = 0x7F000001, // TrafficSpawner::GenerateTraffic
    kDummies, // PedManager::Dummies
    kGenerators,
    kGarages,
    kScripts,
    kEmergency,
    kAmbulance,
    kPolice,
    kFireEngines,
};

// Set by the netcode: the owner key of a ped or a car, the server's net id (0: none, the game's stream)
extern unsigned int (*gPedKey)(const Ped* p);
extern unsigned int (*gCarKey)(const Car* c);
extern bool gOwned; // a network game: services own their draws too
// The last ped and car whose update started (PedPool and CarPool: Pool::UpdatePool), for the crash report
extern const Ped* gLastPed;
extern const Car* gLastCar;

// The owner for the scope's lifetime (the one before it after)
class Scope
{
  public:
    explicit Scope(unsigned int serviceKey);
    explicit Scope(const Ped* p);
    explicit Scope(const Car* c);
    template <class T> explicit Scope(const T*) : mActive(false), mSavedKey(0), mSavedDraws(0)
    {
    } // other pools: whoever owns the draws already
    ~Scope();

  private:
    void Enter(unsigned int key);
    bool mActive;
    unsigned int mSavedKey, mSavedDraws;
};

// The tick that starts (Game::UpdateGame_4B9410, the game's cycle): the owners' draws are numbered per tick
void TickStart(unsigned int cycle);
extern void (*gTickHook)(); // the netcode's, at each tick's start (a client's id maps for the keys)
void SetSeed(unsigned int seed); // the game's seed for the owners' draws (sent to clients with the world)
unsigned int Seed();
} // namespace PortRand
