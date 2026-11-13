// Port (docs/netcode.md, "The game's code"): every hook's definition and default, in net_hooks.hpp's order. port/net/
// sets them; the game's code only reads or calls them.
#include "net_hooks.hpp"

void (*gPortCrashHook)();
void (*gPortSpriteMovedHook)(Sprite*);
void (*gPortIdleHook)();

bool gPortNoAudio;
s32 gPortTrafficCap = 16;
s32 gPortSpawnPlayers = 1;
void (*gPortAfterPlayersHook)();
bool (*gPortWorldStill)();
void (*gPortFrontendSetup)(Frontend* pFrontend);
bool (*gPortFrontendInput)(Frontend* pFrontend);
int (*gPortLeaveNetworkGame)();
void (*gPortDrawOverlay)();
s16 gPortZoneRoundRobin;
bool (*gPortNextNetworkGame)();
bool gPortKeepWantedPoints;
bool gPortServerKillNotices;
