// port/net (port/docs/netcode.md, "The server"): player slots coming into and going out of a running network game. A slot
// without a client is out of the game the way the game handles a player who left (Player::Disconnect:
// its ped walks on as a pedestrian). A slot that gets a client gets a new player ped at the next restart zone,
// as the map script's PLAYER_PED makes one (ScriptThread::Handle_PLAYER_PED) and Player::InitPlayerPed
// sets it up, but with the slot's own weapons kept: InitPlayerPed would allocate a new set each time.
#include "internal.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/GameSession.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/Weapon.hpp"
#include "Source/sprite.hpp"
#include "Source/Car.hpp"
#include "Source/map_0x370.hpp"
#include "Source/ped_graphic_type.hpp"

namespace
{
u8 gRemap[MAX_PLAYERS]; // a slot's look, from the ped the map script gave it
} // namespace

bool net::engine::SlotInGame(int slot)
{
    Player* p = GetPlayer(slot);
    return p && p->mInUse && p->mPlayerPed;
}
// (A slot out of the game keeps its last ped pointer, a pedestrian now, as after Player::Disconnect; only
// slots in the game count anywhere in the netcode)

void net::engine::TakeOutOfGame(int slot)
{
    Player* p = GetPlayer(slot);
    if (!p || !p->mInUse)
    {
        return;
    }
    if (p->mPlayerPed && slot < MAX_PLAYERS && !gRemap[slot])
    {
        gRemap[slot] = (u8)p->mPlayerPed->mRemap;
    }
    if (Ped* ped = p->mPlayerPed)
    {
        // Out of its car first: the ped walks on as a pedestrian, a local slot's camera follows it, and a car it sat in
        // can be despawned under it (the sound read the freed car through the camera)
        if (ped->mCar && !InCarTransition(ped))
        {
            ForceOutBesideCar(ped, ped->mCar);
        }
    }
    p->Disconnect(); // keeps its ped pointer, as the game does: the HUD and camera of a local slot use it
}

// No client in this slot at the game's start: the ped the map script made goes, not a pedestrian in a player's look
// standing at a spawn (PutInGame makes a new one). The slot keeps the pointer, as once a departed player's ped despawns:
// the HUD of the server's own slot reads it
void net::engine::NeverInGame(int slot)
{
    TakeOutOfGame(slot);
    Player* p = GetPlayer(slot);
    Ped* ped = p ? p->mPlayerPed : NULL;
    if (ped && ped->IsActive() && !ped->mCar)
    {
        ped->Deallocate();
    }
}

void net::engine::PutInGame(int slot, bool freshScore)
{
    Player* p = GetPlayer(slot);
    gmp_map_zone* zone = gMap_0x370->GetNextZoneOfTypeRoundRobin(0x10); // a Restart zone, round robin
    if (!p || !zone)
    {
        return;
    }
    if (freshScore && slot < MAX_PLAYERS)
    {
        gGameSession.mFragsList[slot] = 0;
        gGameSession.mPointsList[slot] = 0;
        ResetHudScores(p);
    }
    if (p->mInUse && p->mPlayerPed)
    {
        p->mPlayerPed->RespawnPed(zone); // still in: a fresh start where the others respawn
        // Its camera there too, not panning over for seconds: until it sees the ped, the ped counts as off screen, and
        // a dead player ped off screen for 200 ticks is freed as a corpse (Ped::PoolUpdate), with the player still
        // pointing at it (a crash at its respawn, or once the memory is another ped)
        SnapCamera(p);
        return;
    }
    const Fix16 x = Fix16(zone->mX + zone->mW / 2), y = Fix16(zone->mY + zone->mH / 2);
    const Fix16 z = gMap_0x370->FindGroundZForCoord(x, y);
    const u8 remap = slot < MAX_PLAYERS && gRemap[slot] ? gRemap[slot] : 0;
    Ped* ped = gPedManager->SpawnPedAt(x, y, z, remap, Ang16((s16)0, (u8)0));
    if (!ped)
    {
        return;
    }
    ped->SetPedType(ped_type::player_2);
    ped->set_health(100);
    // Player::InitPlayerPed, without new weapons
    p->mWastedOrBusted = 0;
    p->mBusted = 0;
    p->mAuxPedDying = 0;
    p->mDeathCountdown = 0;
    p->mPlayerPed = ped;
    p->mAuxPed = NULL;
    p->mTrafficSpawnCounter = 0;
    p->mTrafficSpawnThreshold = 1000;
    ped->SetPlayer(p, 0);
    p->mCameraMode = 0;
    for (int w = 0; w < kPlayerWeapons; w++)
    {
        if (Weapon* weapon = p->mWeapons[w])
        {
            weapon->set_pPed(ped);
            weapon->mAmmo = 0;
        }
    }
    p->mCurrWeaponIdx = -1;
    p->mSavedPedWeaponIdx = -1;
    ped->mGraphicType = ped_graphic_type::character_1;
    p->set_in_use(1);
    ped->RespawnPed(zone); // a free spot in the zone, health, a moment of invulnerability
    SnapCamera(p);
}

// For a slot that joins and every slot of a new game, not only at a round's start: else the server's players have
// the multiplier 0 through the warmup
void net::engine::ResetHudScores(Player* p)
{
    p->mScores.SetMoney(0); // the HUD's money, frags and multiplier as at a game's start
    p->mScores.mMultiplayerFrags.SetValueClamped(0);
    p->mMultipliers.SetValueClamped(1);
}

void net::engine::FreshScores(int slot)
{
    if (slot < MAX_PLAYERS) // the game session's tally outlives a game: the last round's frags otherwise
    {
        gGameSession.mFragsList[slot] = 0;
        gGameSession.mPointsList[slot] = 0;
    }
    if (Player* p = GetPlayer(slot))
    {
        ResetHudScores(p);
    }
}
