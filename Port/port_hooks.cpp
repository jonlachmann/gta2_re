// Port (docs/port.md, "`Source/` changes"): every hook's definition and default, in port_hooks.hpp's order. Port/ sets
// them; the game's code only reads or calls them.
#include "port_hooks.hpp"

// ---- The platform layer

void (*gPortCrashHook)();
void (*gPortSpriteMovedHook)(Sprite*);

// ---- Validation and test aids

u32 gPortAttractIdleMs = 60000;

// ---- The network game

s32 gPortTrafficCap = 16;
s32 gPortSpawnPlayers = 1;
void (*gPortAfterPlayersHook)();
bool (*gPortWorldStill)();
void (*gPortFrontendSetup)(Frontend* pFrontend);
bool (*gPortFrontendInput)(Frontend* pFrontend);
int (*gPortLeaveNetworkGame)();
void (*gPortIdleHook)();
void (*gPortDrawOverlay)();
s16 gPortZoneRoundRobin;
bool (*gPortNextNetworkGame)();
bool gPortKeepWantedPoints;
bool gPortServerKillNotices;
