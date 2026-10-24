// Port/net engine internals, shared by the engine's files. Decomp types: only for engine/.
#pragma once
#include "engine.hpp"
#include "Source/ang16.hpp"
#include "Source/fix16.hpp"
#include <stddef.h>
#include <unordered_map>

class Car;
class Object;
class Ped;
class Player;
class Sprite;

namespace net
{
namespace engine
{
// The schema's fields (schema.cpp builds the schema in this order). Every entity with a place has x, y, z and rot
// first (PutPosition)
const int kPlayerWeapons = 28; // Player::field_718_weapons: its own (0..14) and its car weapons (15..27)
const int kPowerUps = 17; // Player::field_6F4_power_up_timers
enum PlayerField
{
    kPlayerX,
    kPlayerY,
    kPlayerZ,
    kPlayerRot,
    kPlayerInCar,
    kPlayerButtons,
    kPlayerHealth,
    kPlayerDeaths, // how often the server's player died (wraps at 256)
    kPlayerKiller, // who killed it last: player index + 1, 0 nobody
    kPlayerWeapon, // Player::field_788_curr_weapon_idx + 1 (0: none)
    kPlayerFrags,
    kPlayerPoints,
    kPlayerAmmo0, // each weapon's ammo
    kPlayerCar = kPlayerAmmo0 + kPlayerWeapons, // the car it drives (CarRef)
    kPlayerState, // kPlayerTransition | kPlayerDead | kPlayerLeaving
    kPlayerTarget, // the car it is going to get into (Ped::field_150_target_objective_car, CarRef)
    kPlayerName, // its name (Player::field_83C_player_name), 4 characters a field
    kPlayerNameEnd = kPlayerName + 4,
    kPlayerPowerUp = kPlayerNameEnd, // the power-up timers
    kPlayerPowerUpEnd = kPlayerPowerUp + kPowerUps,
    kPlayerChatting = kPlayerPowerUpEnd, // typing a chat line (Player::field_794_is_chatting, Hud_ChatInput)
    kPlayerChat, // the line so far (field_796_chat_text), 4 characters a field
    kPlayerChatEnd = kPlayerChat + 20,
    kPlayerWanted = kPlayerChatEnd, // the wanted points (Ped::field_20A_wanted_points: the HUD's cop heads, the police's chase)
};
enum PlayerState
{
    kPlayerTransition = 1, // getting into or out of a car (InCarTransition)
    kPlayerDead = 2,
    kPlayerLeaving = 4, // getting out of one (LeavingCar)
};
enum WorldField
{
    kWorldCycle, // the game's tick counter (gpRng_67AB34's field_0_rng): timers are stamped with it (a car's damage, a ped's voice...)
    kWorldRand, // the game's random state (rand(), Port/port_rand.cpp): a resimulation draws what the server drew
    kWorldReach, // how far this client's snapshot reaches (blocks x 16): the farthest it kept, the rest cut for size
    kWorldIt, // the tag game's "it" (TagGame::field_0_fugitive): player index + 1, 0 nobody
    kWorldItTime, // and the seconds each player has been it (field_4_it_time_secs), MAX_PLAYERS of them
    kWorldZoneTurn = kWorldItTime + 6, // which restart zone is next (Map_0x370::GetNextZoneOfTypeRoundRobin_4DF0F0): a respawn here goes where the server's does
    kWorldRound, // the round (rounds.cpp): its number, the last one's winner (player + 1) and the seconds left
    kWorldWinner,
    kWorldSecondsLeft,
    kWorldGame, // the server's game since its start (GameGeneration): a client takes only its current one's
};
enum CarField
{
    kCarX,
    kCarY,
    kCarZ,
    kCarRot,
    kCarModel,
    kCarDriver, // kDriverNone, kDriverTraffic (the car manager's shared spare driver), kDriverPed (sent as a ped entity)
    kCarDamage, // Car::field_74_damage (32000 and up: exploded), the server's
    kCarAreas, // its dented areas (Car::field_8_damaged_areas)
    kCarWrecked, // exploded or burning out (the wreck countdown running, or a wreck)
    kCarLights, // Car::field_A4_light_flags (brake, head, emergency lights...), field_A5 flash phase, field_A7 horn, field_A8 horn cooldown
    kCarFlash,
    kCarHorn,
    kCarHornCooldown,
    kCarRemap, // its colour (the sprite's remap): a respray, or a car made here, had another
    kCarPhysics, // then its whole CarPhysics, member by member (cars.cpp; all 0 without physics), then its AI (car_ai.cpp)
};
enum CarDriver
{
    kDriverNone = 0,
    kDriverTraffic = 1,
    kDriverPed = 2,
};
enum PedField
{
    kPedX,
    kPedY,
    kPedZ,
    kPedRot,
    kPedRemap,
    kPedCar, // the car it drives (CarRef; its position is the car's then)
    kPedState, // 1: getting into or out of a car
    kPedAi, // then its AI state (ped_state.cpp)
};
enum ObjectField
{
    kObjX,
    kObjY,
    kObjZ,
    kObjRot,
    kObjModel, // Object::field_18_model (objects::)
    kObjIdx, // Object::field_26_ped_ref_idx (what kind, for some)
};

// Shared helpers (access.cpp)
Player* GetPlayer(int slot); // NULL past the game's players
Ped* InGamePed(int slot); // its ped while the slot is in the game (Player::field_8E_in_use)
inline Fix16 Raw(int32_t v) // a field's position value
{
    return Fix16(v, 0);
}
inline Ang16 Angle(int32_t v)
{
    return Ang16((s16)v, (u8)0);
}
// A car as a field: Car::field_6C_car_id + 1, 0 none; and back to this game's car for it (NULL: none or not here)
int32_t CarRef(const Car* car);
uint32_t CarRefNetId(int32_t ref);
Car* RefCar(int32_t ref);
Sprite* CharSprite(const Ped* ped); // its char's (on foot), NULL without one
Sprite* PedSprite(Ped* ped); // its car's when it is in one
EntityState Entity(uint32_t id, uint8_t type);
void PutPosition(EntityState& e, const Sprite* s); // fields 0..3
bool Near(const Sprite* s, int32_t x, int32_t y, int32_t radius); // within <radius> (raw) on both axes
const EntityState* FindWorld(const Snapshot& snap);
// The area a snapshot covered around the local player, less a margin (blocks): what the snapshot lacks is gone only
// inside it (beyond, a busy area's snapshot was cut for size). Without a local player, nothing is inside or beyond
class ReachArea
{
  public:
    ReachArea(const Snapshot& snap, float margin);
    bool Inside(const Sprite* s) const;
    bool Beyond(const Sprite* s) const;
    float DistanceTo(const Sprite* s) const;

  private:
    bool mLocated;
    float mX = 0, mY = 0, mReach;
};
// The one dummy ped every traffic car shares as its driver (Car::SpawnDriverPed, CarManager::field_4_spare_driver): no
// ped of its own, never sent, moved, forced or removed
bool IsSpareDriver(const Ped* ped);
// Moved and turned at once, the collision grid kept right (cars.cpp, access.cpp)
void MoveCar(Car* car, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot);
void MovePed(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot);
void CapturePed(Ped* p, Car* driving, Snapshot& out);

// The debug switches (GTA2_NET_*), read once (debug.cpp)
struct DebugFlags
{
    bool carDiff, selfLog, animLog, soundLog, checkGrid, pickupLog;
};
const DebugFlags& Debug();
bool DebugGridOk(const char* where, Car* car); // GTA2_NET_CHECKGRID: the collision grid has no freed sprites
void DebugCarDiff(Car* car, const EntityState& e, uint32_t tick); // GTA2_NET_CARDIFF: the physics members that differ (cars.cpp)

// Players (players.cpp)
void CapturePlayer(int slot, Snapshot& out);
void ApplyWorld(const EntityState& w);
void ApplySlots(const Snapshot& snap); // the slots in the game: the snapshot's
void ApplyPlayer(const EntityState& e);
void NetDeaths_Memory(unsigned char** ppAddr, size_t* pSize); // the deaths a client has carried out (in the rollback set: world.cpp)
void KillCredited(Ped* ped, int killerSlot); // the game's own death, credited to a player (-1: nobody)
void SnapCamera(Player* p);
void ResetDeaths(int slot); // the death count the snapshots carry (players.cpp) // the camera at its ped at once, not panning there
// Player peds take no damage on a client: their health and deaths are the server's (client_mode.cpp)
void ProtectPlayers();
void SeenByAll(); // every ped and car counts as seen (client_mode.cpp): no removal off screen on a client
// Before and after every tick (client_mode.cpp): what the server's game never has, this one can't run (a ped's on-foot
// order in a car)

// Cars (cars.cpp, car_ai.cpp)
void CarPhysicsFields(std::vector<FieldDesc>& out);
void CaptureCar(Car* c, Snapshot& out);
bool CarHasPhysics(const EntityState& e); // a car entity's physics is there (awake): not all zeros
void SetCarPhysics(Car* car, const EntityState& e); // the physics the snapshot has for it, after MoveCar
// Whether it has exploded (or is burning out); its state as the server has it: an explosion the server had is carried
// out (the game's own), damage, dents, lights, horn and colour are set. False: it is a wreck here and not there (it has
// to be made again)
bool IsWrecked(const Car* car);
bool SetCarState(Car* car, const EntityState& e);
extern int gExplosionsCarried, gWrecksReplaced; // for the log
void CarAiFields(std::vector<FieldDesc>& out); // after the physics
void CaptureCarAi(const Car* car, EntityState& e);
void ApplyCarAi(Car* car, const EntityState& e);

// Getting into and out of cars (vehicles.cpp): at once, ending as the game's own entering and leaving do; or started as
// the enter key starts it
bool InCarTransition(Ped* ped); // on its way into or out of a car: states, or an objective to do it
bool LeavingCar(Ped* ped); // on its way out of one
bool AtCarDoor(Ped* ped); // getting in or out at the door (not the walk there)
void ForceIntoCar(Ped* ped, Car* car);
void ForceOutOfCar(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot);
void ForceOutBesideCar(Ped* ped, Car* car); // out at the car's side (a car's driver needn't point back to it)
void StartEntering(Ped* ped, Car* car);
void StartLeaving(Ped* ped);

// A client's cars and peds (replica.cpp)
void ApplyCarsAndPeds(const Snapshot& snap);
Car* ReplicaCar(uint32_t netId); // this game's car for a server car, if it has one
Ped* ReplicaPed(uint32_t netId); // and ped
const std::unordered_map<uint32_t, Car*>& ReplicaCars();
void ClearReplicas(); // a new game: nothing mapped (rounds.cpp)
void ClearPickups();
const std::unordered_map<uint32_t, Ped*>& ReplicaPeds();
void ReplicaPoses(std::vector<Pose>& out); // the cars and peds of Poses/SetPose
void SetReplicaPose(const Pose& p);
// The replica mapping, saved and restored with the world (rollback.cpp): an opaque copy
struct ReplicaState;
ReplicaState* SaveReplicas(ReplicaState* reuse);
void RestoreReplicas(const ReplicaState* state);
void FreeReplicas(ReplicaState* state);

// A ped's AI state (ped_state.cpp): its fields (kPedAi on), capture, apply. Apply: false when the ped is dead here and
// not on the server (it has to be made again)
void PedStateFields(std::vector<FieldDesc>& out);
void CapturePedState(const Ped* p, EntityState& e);
bool ApplyPedState(Ped* p, const EntityState& e);
void ApplyPedGroups(const std::vector<std::pair<Ped*, const EntityState*>>& peds); // once the snapshot's peds are here

// Pickups (pickups.cpp): the server's in a snapshot, and a client's made to match
void CapturePickups(Snapshot& out);
void ApplyPickups(const Snapshot& snap);
std::unordered_map<uint32_t, Object*>& PickupMap(); // for the rollback (ReplicaState)
extern int gPickupsMade, gPickupsRemoved; // for the log
} // namespace engine
} // namespace net
