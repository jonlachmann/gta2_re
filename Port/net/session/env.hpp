// Port/net (docs/netcode.md, "Environment variables"): the session's GTA2_NET_* switches, read once (env.cpp). Most
// are for the checks and test hooks (Port/net/dev).
#pragma once
#include <stdint.h>

namespace net
{
// The session's GTA2_NET_* switches (docs/netcode.md), read once
struct SessionEnv
{
    const char* record; // a file to record the client's session to
    bool marker; // the server's position of the local player drawn
    bool corrLog, animLog, inputLog;
    bool compare; // the comparison check: the prediction against each snapshot, field by field
    uint32_t divergeTick, divergeTicks; // the divergence check: from that server tick on this game alone, for that many ticks
    int jerkLog; // 1: the jerk statistics, 2: and each jump
    float blend; // how far a correction is blended in each frame (0.2; 1: at once)
    const char* navDump; // the bots' map as a picture
    const char* testKill; // <tick>:<victim>:<killer>[,...]: server test kills
    const char* testArm; // <weapon>:<ammo>: every player kept armed on the server
    const char* testWanted; // <points>: every player kept at least that wanted on the server
    const char* testPowerUp; // <tick>:<player>:<power-up>[,...]: server test power-ups
    const char* testCar; // <tick>:<player>:<model>[,...]: server test cars beside players (11: a bus)
    const char* say; // <frame>:<command>[;...]: a client's test commands to the server ("/kick r_b")
};
const SessionEnv& Env();
} // namespace net
