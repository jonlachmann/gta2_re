// port/net (port/docs/netcode.md, "Entities"): the players as snapshot entities,
// and the server's put into a client's game: health, deaths, weapons, power-ups, its car, its place.
#include "internal.hpp"
#include "world.hpp"
#include "dev/dev.hpp"
#include "port/src/rand/owner.hpp"
#include "port/src/net_hooks.hpp"
#include "port/src/rand/state.hpp"
#include "Source/Camera.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/GameSession.hpp"
#include "Source/Hud.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/TrafficLights.hpp"
#include "Source/Weapon.hpp"
#include "Source/char.hpp"
#include "Source/network_data.hpp"
#include "Source/rng.hpp"
#include "Source/sprite.hpp"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#define sPrevNetInputs PortStatic<Network_PlayerInputs>(0x6F5B28) // winmain.cpp's file static

namespace
{
using namespace net::engine;
using net::EntityState;
using net::Snapshot;

// The server's death counts per player, one more each time Player::mWastedOrBusted turns on: the round's
// (the results; a new round starts them again) and the game's, which only counts up (what a client carries out)
uint8_t gDeaths[MAX_PLAYERS];
uint8_t gDeathEvents[MAX_PLAYERS];
uint8_t gKiller[MAX_PLAYERS];
bool gWasDead[MAX_PLAYERS];
// A client's: the deaths it has carried out, by the server's game count, and whether it has seen the player yet (it
// takes the count as it finds it the first time: a joiner doesn't carry out the deaths before it came), in the
// rollback set
struct
{
    uint8_t mDone[MAX_PLAYERS];
    bool mKnown[MAX_PLAYERS];
} gNetDeaths;

int PlayerIndexOfPed(s32 pedId)
{
    Ped* killer = pedId ? gPedManager->PedById(pedId) : NULL;
    return killer && killer->mPlayer ? killer->mPlayer->mPlayerIdx : -1;
}

std::string NarrowName(const Player* p)
{
    std::string n;
    for (int k = 0; k < 16 && p->mPlayerName[k]; k++)
    {
        n += (char)p->mPlayerName[k];
    }
    return n;
}

// 4 characters a field
void PackName(const Player* p, net::EntityState& e)
{
    for (int k = 0; k < 16; k++)
    {
        e.f[kPlayerName + k / 4] |= (int32_t)((uint32_t)(p->mPlayerName[k] & 0xFF) << (8 * (k % 4)));
    }
}

// The chat line being typed, as the name: the low byte of each character, 4 a field
void PackChat(const Player* p, net::EntityState& e)
{
    e.f[kPlayerChatting] = p->mIsChatting != 0;
    for (int k = 0; k < 79 && p->mChatText[k]; k++)
    {
        e.f[kPlayerChat + k / 4] |= (int32_t)((uint32_t)(p->mChatText[k] & 0xFF) << (8 * (k % 4)));
    }
}

void UnpackChat(Player* p, const net::EntityState& e)
{
    s16 text[79];
    int n = 0;
    for (; n < 78; n++)
    {
        text[n] = (s16)(((uint32_t)e.f[kPlayerChat + n / 4] >> (8 * (n % 4))) & 0xFF);
        if (!text[n])
        {
            break;
        }
    }
    text[n] = 0;
    const bool chatting = e.f[kPlayerChatting] != 0;
    if (chatting && (n != p->mChatTextIdx || memcmp(text, p->mChatText, (n + 1) * sizeof(s16))))
    {
        std::string line;
        for (int k = 0; k < n; k++)
        {
            line += (char)text[k];
        }
        printf("net: chat, %s: %s\n", NarrowName(p).c_str(), line.c_str()); // the log's copy of the line as typed
    }
    p->mIsChatting = (char_type)chatting;
    memcpy(p->mChatText, text, (n + 1) * sizeof(s16));
    p->mChatTextIdx = n;
}

std::string UnpackName(const net::EntityState& e)
{
    char name[17] = {};
    for (int k = 0; k < 16; k++)
    {
        name[k] = (char)(((uint32_t)e.f[kPlayerName + k / 4] >> (8 * (k % 4))) & 0xFF);
    }
    return name;
}

void CountDeath(int i, Player* p, Ped* ped)
{
    if (i >= MAX_PLAYERS)
    {
        return;
    }
    const bool dead = p->mWastedOrBusted != 0;
    if (dead && !gWasDead[i])
    {
        gDeaths[i]++;
        gDeathEvents[i]++;
        Ped* killer = ped->mKillerId ? gPedManager->PedById(ped->mKillerId) : NULL;
        gKiller[i] = killer && killer->IsLawEnforcement() ? kKillerLaw : (uint8_t)(PlayerIndexOfPed(ped->mKillerId) + 1);
    }
    gWasDead[i] = dead;
}

// Weapons and ammo are the server's
void ApplyWeapons(Player* p, const net::EntityState& e)
{
    // Only to a weapon this game has made for the player (a new map's game: not yet, and the HUD reads it,
    // Hud_PlayerStats::DrawPlayerStats)
    if (e.f[kPlayerWeapon] >= 1 && e.f[kPlayerWeapon] <= kPlayerWeapons && p->mWeapons[e.f[kPlayerWeapon] - 1])
    {
        p->mCurrWeaponIdx = (s16)(e.f[kPlayerWeapon] - 1);
    }
    for (int w = 0; w < kPlayerWeapons; w++)
    {
        if (p->mWeapons[w])
        {
            p->mWeapons[w]->mAmmo = (u16)e.f[kPlayerAmmo0 + w];
        }
    }
}

// Power-ups too (one collected here in a prediction the server didn't have ends here), with what they switch on the
// ped (Player::CollectPowerUp, tick_down_powerups); invulnerability is the client's anyway
void ApplyPowerUps(Player* p, Ped* ped, const net::EntityState& e)
{
    for (int k = 0; k < kPowerUps; k++)
    {
        const u16 now = (u16)e.f[kPlayerPowerUp + k];
        if (!p->mPowerUpTimers[k] != !now)
        {
            if (k == power_up_indices::Electrofingers_9)
            {
                now ? ped->SetElectrofingers() : ped->ClearElectrofingers();
            }
            else if (k == power_up_indices::Invisibility_11)
            {
                now ? ped->SetInvisible() : ped->SetVisible();
            }
        }
        p->mPowerUpTimers[k] = now;
    }
}

// The HUD's notice of a death the server had, as Ped::UpdateStatsForKiller gives it there: by a player, or else
// a suicide, but none for the police's (a client's own notices are off, gPortServerKillNotices)
void AnnounceServerKill(Player* victim, int killer)
{
    if (killer == kKillerLaw)
    {
        return;
    }
    Player* by = killer ? GetPlayer(killer - 1) : NULL;
    gPortServerKillNotices = false;
    gHud_2B00->mMpMessage.AnnounceKill(by ? by : victim, victim);
    gPortServerKillNotices = true;
}

// A death the server had and this game hasn't carried out: the game's own death, credited to the killer, where this
// game has the ped (the server has already respawned it: the next snapshot brings it there). True: carried out now
bool CarryOutDeath(int i, Player* p, Ped* ped, const net::EntityState& e)
{
    if (i >= MAX_PLAYERS)
    {
        return false;
    }
    const uint8_t events = (uint8_t)e.f[kPlayerDeathEvents];
    if (!gNetDeaths.mKnown[i])
    {
        gNetDeaths.mKnown[i] = true;
        gNetDeaths.mDone[i] = events;
        return false;
    }
    if (events == gNetDeaths.mDone[i])
    {
        return false;
    }
    gNetDeaths.mDone[i] = events;
    AnnounceServerKill(p, e.f[kPlayerKiller]);
    if (ped->isDead() || p->mWastedOrBusted)
    {
        return false;
    }
    if (ped->mCar)
    {
        // Out of the car first, the way leaving one ends: the game kills a player at the wheel only with its car (and
        // clears the driver then); killed in its seat, the car kept the respawned ped as its driver, and a ped pulling
        // that "driver" out gave it a second char (left in the collision grid)
        ForceOutOfCar(ped, Raw(e.f[kPlayerX]), Raw(e.f[kPlayerY]), Raw(e.f[kPlayerZ]), Angle(e.f[kPlayerRot]));
    }
    KillCredited(ped, e.f[kPlayerKiller] == kKillerLaw ? -1 : e.f[kPlayerKiller] - 1);
    return true;
}

// The car it is going to get into, as the server has it, while it is still on its way (not at the door yet): in a jam
// the nearest car in front (CarManager::GetNearestFrontVehicle) can be another one here
void RetargetEntering(Ped* ped, Car* serverTarget, const net::EntityState& e)
{
    if (serverTarget && serverTarget->mCarSprite && !ped->mCar && !ped->isDead() && !(e.f[kPlayerState] & kPlayerDead) &&
        ped->get_objective() == objectives_enum::enter_car_as_driver_35 && ped->mTargetObjectiveCar != serverTarget &&
        ped->mPedState1 != ped_state_1::entering_car_3)
    {
        StartEntering(ped, serverTarget);
    }
}

// In the server's car, or on foot as the server has it: predicted by this game's own input, put right here when both
// sides have settled (neither halfway into or out of a car, nobody dead) and not counting down to its respawn here
// (wasted): the ped waits at the restart zone in a plain state, and put into the server's car (respawned there
// already) its char went, which the respawn reads (Ped::ResetForPlayerRespawn)
void SettleCar(Player* p, Ped* ped, const net::EntityState& e)
{
    const bool settled = !(e.f[kPlayerState] & (kPlayerTransition | kPlayerDead)) && !InCarTransition(ped) && !ped->isDead() &&
                         !p->mWastedOrBusted;
    if (!settled)
    {
        return;
    }
    Car* serverCar = RefCar(e.f[kPlayerCar]);
    if (serverCar && ped->mCar != serverCar)
    {
        const GridCheck check("forcing into a car", serverCar);
        ForceIntoCar(ped, serverCar);
    }
    else if (!e.f[kPlayerCar] && ped->mCar)
    {
        Car* was = ped->mCar;
        const GridCheck check("forcing out of a car", was);
        ForceOutOfCar(ped, Raw(e.f[kPlayerX]), Raw(e.f[kPlayerY]), Raw(e.f[kPlayerZ]), Angle(e.f[kPlayerRot]));
    }
}

// Its place: its car's, or its own on foot. True: a jump of the local player (its first snapshot, a respawn), which
// the camera follows at once instead of panning across the map, as PutInGame and the game's respawn place it
bool MovePlayer(int i, Ped* ped, const net::EntityState& e)
{
    const Fix16 x = Raw(e.f[kPlayerX]), y = Raw(e.f[kPlayerY]), z = Raw(e.f[kPlayerZ]);
    const Ang16 rot = Angle(e.f[kPlayerRot]);
    const GridCheck check("moving a player", ped->mCar);
    Sprite* was = PedSprite(ped);
    const bool jump = i == LocalPlayer() && was && (abs(was->mXy.x.mValue - x.mValue) > (8 << 14) || abs(was->mXy.y.mValue - y.mValue) > (8 << 14));
    if (ped->mCar)
    {
        MoveCar(ped->mCar, x, y, z, rot);
    }
    else
    {
        MovePed(ped, x, y, z, rot);
    }
    return jump;
}

// The server's player on its way into or out of a car and this game's not: another player's key press came before
// the ticks this game resimulates, so its ped stood by the car until the server had it inside. Started here from the
// server's position, as the key starts it (Player::HandleControls)
void StartTransition(Player* p, Ped* ped, Car* serverTarget, const net::EntityState& e)
{
    if ((e.f[kPlayerState] & (kPlayerTransition | kPlayerDead)) != kPlayerTransition || InCarTransition(ped) || ped->isDead() ||
        p->mWastedOrBusted)
    {
        return;
    }
    Car* car = ped->mCar;
    if (!car && serverTarget && serverTarget->mCarSprite && !(e.f[kPlayerState] & kPlayerLeaving) && ped->mGameObject)
    {
        StartEntering(ped, serverTarget);
    }
    else if (car && (e.f[kPlayerState] & kPlayerLeaving) && RefCar(e.f[kPlayerCar]) == car && car->CanExitCar())
    {
        StartLeaving(ped);
    }
}
} // namespace

void net::engine::CapturePlayer(int i, Snapshot& out)
{
    Player* p = GetPlayer(i);
    Ped* ped = InGamePed(i); // slots out of the game aren't sent
    Sprite* s = ped ? PedSprite(ped) : NULL;
    if (!s)
    {
        return;
    }
    EntityState e = Entity(1 + i, kEntityPlayer);
    PutPosition(e, s);
    e.f[kPlayerInCar] = ped->mCar != NULL;
    // The controls the tick ran with (winmain.cpp's Net_Set_Local_Player_Inputs takes them from here; the
    // player's own copy, Player::mInputs, is zeroed once used): what clients predict this player with
    e.f[kPlayerButtons] = (int32_t)(sPrevNetInputs.mInputs[i].mInputs & 0x3FFFFF);
    e.f[kPlayerHealth] = ped->mHealth;
    CountDeath(i, p, ped);
    e.f[kPlayerDeaths] = i < MAX_PLAYERS ? gDeaths[i] : 0;
    e.f[kPlayerDeathEvents] = i < MAX_PLAYERS ? gDeathEvents[i] : 0;
    e.f[kPlayerKiller] = i < MAX_PLAYERS ? gKiller[i] : 0;
    e.f[kPlayerWeapon] = p->mCurrWeaponIdx + 1;
    e.f[kPlayerFrags] = gGameSession.mFragsList[i];
    e.f[kPlayerPoints] = gGameSession.mPointsList[i];
    for (int w = 0; w < kPlayerWeapons; w++)
    {
        e.f[kPlayerAmmo0 + w] = p->mWeapons[w] ? p->mWeapons[w]->mAmmo : 0;
    }
    e.f[kPlayerCar] = CarRef(ped->mCar);
    e.f[kPlayerState] = (InCarTransition(ped) ? kPlayerTransition : 0) | (ped->isDead() ? kPlayerDead : 0) | (LeavingCar(ped) ? kPlayerLeaving : 0);
    e.f[kPlayerTarget] = ped->get_objective() == objectives_enum::enter_car_as_driver_35 ? CarRef(ped->mTargetObjectiveCar) : 0;
    PackName(p, e);
    PackChat(p, e);
    e.f[kPlayerWanted] = ped->mWantedPoints;
    e.f[kPlayerMoney] = p->mScores.mMoney.mValue;
    e.f[kPlayerMultiplier] = p->mMultipliers.mValue;
    for (int k = 0; k < kPowerUps; k++)
    {
        e.f[kPlayerPowerUp + k] = p->mPowerUpTimers[k];
    }
    out.entities.push_back(e);
}

void net::engine::ApplySlots(const Snapshot& snap)
{
    // The slots the server has in the game, and only those (a join or a leave on the server: the same here)
    bool sent[MAX_PLAYERS] = {};
    for (const EntityState& e : snap.entities)
    {
        if (e.type == kEntityPlayer && e.id >= 1 && e.id <= MAX_PLAYERS)
        {
            sent[e.id - 1] = true;
            if (!SlotInGame((int)e.id - 1))
            {
                PutInGame((int)e.id - 1, false); // its place and score follow from the snapshot
            }
        }
    }
    for (int i = 0; i < PlayerCount() && i < MAX_PLAYERS; i++)
    {
        if (!sent[i] && SlotInGame(i))
        {
            TakeOutOfGame(i);
        }
    }
}

void net::engine::ApplyPlayer(const EntityState& e)
{
    const int i = (int)e.id - 1;
    Player* p = GetPlayer(i);
    Ped* ped = p ? p->mPlayerPed : NULL;
    if (!ped)
    {
        return;
    }
    const std::string name = UnpackName(e);
    if (!name.empty() && NarrowName(p) != name)
    {
        SetPlayerName(i, name.c_str());
    }
    ApplyWeapons(p, e);
    ApplyPowerUps(p, ped, e);
    // Its wanted points, the server's: the HUD's cop heads, and the cops' AI here, which gave up the chase at 0 stars
    ped->mWantedPoints = (s16)e.f[kPlayerWanted];
    if (i != LocalPlayer())
    {
        UnpackChat(p, e); // the others' typing; ours is this game's (KeepLocalChat)
    }
    if (CarryOutDeath(i, p, ped, e))
    {
        return;
    }
    if (!ped->isDead())
    {
        // At least 1: a death here comes from the server's death count (above), whole; the health the server's dying
        // ped has (0) started a death of this game's own, undone by the next restore (a flicker of dying and living)
        ped->set_health((s16)(e.f[kPlayerHealth] > 0 ? e.f[kPlayerHealth] : 1));
    }
    Car* serverTarget = RefCar(e.f[kPlayerTarget]);
    RetargetEntering(ped, serverTarget, e);
    SettleCar(p, ped, e);
    const bool jump = MovePlayer(i, ped, e);
    StartTransition(p, ped, serverTarget, e);
    if (jump)
    {
        SnapCamera(p);
    }
}

void net::engine::ApplyScores(const Snapshot& snap)
{
    for (const EntityState& e : snap.entities)
    {
        const int i = (int)e.id - 1;
        if (e.type == kEntityPlayer && i >= 0 && i < MAX_PLAYERS)
        {
            gGameSession.mFragsList[i] = (s16)e.f[kPlayerFrags];
            gGameSession.mPointsList[i] = e.f[kPlayerPoints];
            if (Player* p = GetPlayer(i)) // the HUD's money, frags and multiplier: the server's, as the scores
            {
                p->mScores.SetMoney(e.f[kPlayerMoney]);
                p->mScores.mMultiplayerFrags.SetValueClamped(e.f[kPlayerFrags]); // the HUD's own copy
                p->mMultipliers.SetValueClamped(e.f[kPlayerMultiplier]);
            }
        }
    }
}

void net::engine::NetDeaths_Memory(unsigned char** ppAddr, size_t* pSize)
{
    *ppAddr = (unsigned char*)&gNetDeaths;
    *pSize = sizeof(gNetDeaths);
}

void net::engine::KillCredited(Ped* ped, int killerSlot)
{
    Player* killer = GetPlayer(killerSlot);
    ped->mKillerId = killer && killer->mPlayerPed ? killer->mPlayerPed->mId : 0;
    ped->mInvulnerability = 0;
    ped->Kill();
}

void net::engine::SnapCamera(Player* p)
{
    p->mGameCamera.UpdateFollowPedCamera(p->mPlayerPed);
    p->mGameCamera.CommitPendingTarget();
    p->mGameCamera.ResetCameraSmoothing();
}

bool net::engine::PlayerOf(const EntityState& e, int* pPlayer, uint32_t* pButtons)
{
    if (e.type != kEntityPlayer)
    {
        return false;
    }
    *pPlayer = (int)e.id - 1;
    *pButtons = (uint32_t)e.f[kPlayerButtons];
    return true;
}

int net::engine::PlayerHealth(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerHealth] : 0;
}

int net::engine::PlayerPoints(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerPoints] : 0;
}

int net::engine::PlayerFrags(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerFrags] : 0;
}

uint32_t net::engine::PlayerCarId(const EntityState& e)
{
    return e.type == kEntityPlayer ? CarRefNetId(e.f[kPlayerCar]) : 0;
}

int net::engine::PlayerWeapon(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerWeapon] - 1 : -1;
}

int net::engine::PlayerAmmo(const EntityState& e, int weapon)
{
    return e.type == kEntityPlayer && weapon >= 0 && weapon < kPlayerWeapons ? e.f[kPlayerAmmo0 + weapon] : 0;
}

int net::engine::PlayerDeaths(const EntityState& e)
{
    return e.type == kEntityPlayer ? e.f[kPlayerDeaths] : 0;
}

bool net::engine::PlayerPose(int player, float* pX, float* pY, float* pZ, int* pRot)
{
    Player* p = GetPlayer(player);
    Sprite* s = p && p->mPlayerPed ? PedSprite(p->mPlayerPed) : NULL;
    if (!s)
    {
        return false;
    }
    *pX = s->mXy.x.ToFloat();
    *pY = s->mXy.y.ToFloat();
    *pZ = s->mZpos.ToFloat();
    *pRot = s->mRotation.rValue;
    return true;
}

bool net::engine::PlayerPosition(int player, float* pX, float* pY)
{
    float z;
    int rot;
    return PlayerPose(player, pX, pY, &z, &rot);
}

bool net::engine::PlayerWasted(int player)
{
    Player* p = GetPlayer(player);
    return p && p->mWastedOrBusted;
}

bool net::engine::PlayerInCar(int player)
{
    Ped* ped = InGamePed(player);
    return ped && ped->mCar;
}

std::string net::engine::PlayerName(int slot)
{
    Player* p = GetPlayer(slot);
    return p ? NarrowName(p) : std::string();
}

void net::engine::ResetDeaths(int slot)
{
    if (slot >= 0 && slot < MAX_PLAYERS)
    {
        gDeaths[slot] = 0;
        gKiller[slot] = 0;
    }
}

void net::engine::ResetDeathEvents()
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        gDeathEvents[i] = 0;
        gNetDeaths.mKnown[i] = false; // a client takes the new game's counts as it finds them
    }
}

net::engine::LocalChat net::engine::TakeLocalChat()
{
    LocalChat c;
    if (Player* p = GetPlayer(LocalPlayer()))
    {
        c.chatting = p->mIsChatting;
        c.length = p->mChatTextIdx;
        memcpy(c.text, p->mChatText, sizeof(c.text));
    }
    return c;
}

void net::engine::PutLocalChat(const LocalChat& c)
{
    if (Player* p = GetPlayer(LocalPlayer()))
    {
        p->mIsChatting = c.chatting;
        p->mChatTextIdx = c.length;
        memcpy(p->mChatText, c.text, sizeof(c.text));
    }
}

void net::engine::SetPlayerName(int slot, const char* name)
{
    Player* p = GetPlayer(slot);
    if (!p)
    {
        return;
    }
    int k = 0;
    for (; k < 15 && name[k]; k++)
    {
        p->mPlayerName[k] = (wchar_t)(unsigned char)name[k];
    }
    p->mPlayerName[k] = 0;
    gGameSession.SetPlayerName((u16)slot, p->mPlayerName); // as Player.cpp sets it up
}
