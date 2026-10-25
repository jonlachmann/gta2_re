// Port/net: the session's GTA2_NET_* switches (session.hpp), read once.
#include "session.hpp"
#include <stdlib.h>

const net::SessionEnv& net::Env()
{
    static const SessionEnv sEnv = [] {
        SessionEnv e;
        e.record = getenv("GTA2_NET_RECORD");
        e.marker = getenv("GTA2_NET_MARKER") != NULL;
        e.corrLog = getenv("GTA2_NET_CORRLOG") != NULL;
        e.animLog = getenv("GTA2_NET_ANIMLOG") != NULL;
        e.inputLog = getenv("GTA2_NET_INPUTLOG") != NULL;
        e.jerkLog = getenv("GTA2_NET_JERKLOG") ? (atoi(getenv("GTA2_NET_JERKLOG")) >= 2 ? 2 : 1) : 0;
        e.blend = getenv("GTA2_NET_BLEND") ? (float)atof(getenv("GTA2_NET_BLEND")) : 0.2f; // 0.3: remote players a fifth jerkier
        e.navDump = getenv("GTA2_NET_NAVDUMP");
        e.testKill = getenv("GTA2_NET_TESTKILL");
        e.testArm = getenv("GTA2_NET_TESTARM");
        e.compare = getenv("GTA2_NET_COMPARE") != NULL;
        e.testPowerUp = getenv("GTA2_NET_TESTPOWERUP");
        e.testWanted = getenv("GTA2_NET_TESTWANTED");
        return e;
    }();
    return sEnv;
}
