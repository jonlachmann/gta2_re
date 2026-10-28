#include "input_script.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace
{
struct KeyEvent
{
    unsigned mFrame;
    unsigned mDik;
    bool mDown;
};

std::vector<KeyEvent> gEvents; // sorted by frame
size_t gNext;
const unsigned* gFrameCounter;
FILE* gKeyLog;
DWORD gSequence = 1;

unsigned Frame()
{
    return gFrameCounter ? *gFrameCounter : 0;
}

void Parse(const char* s)
{
    while (s && *s)
    {
        char* end = 0;
        KeyEvent down;
        down.mFrame = (unsigned)strtoul(s, &end, 10);
        if (*end != ':')
        {
            break;
        }
        down.mDik = (unsigned)strtoul(end + 1, &end, 0);
        down.mDown = true;
        unsigned held = 2;
        if (*end == ':')
        {
            held = (unsigned)strtoul(end + 1, &end, 10);
        }
        KeyEvent up = down;
        up.mFrame += held;
        up.mDown = false;
        gEvents.push_back(down);
        gEvents.push_back(up);
        s = (*end == ',') ? end + 1 : 0;
    }
    // Stable insertion sort by frame keeps the down/up order of each key.
    for (size_t i = 1; i < gEvents.size(); i++)
    {
        KeyEvent e = gEvents[i];
        size_t j = i;
        while (j > 0 && gEvents[j - 1].mFrame > e.mFrame)
        {
            gEvents[j] = gEvents[j - 1];
            j--;
        }
        gEvents[j] = e;
    }
}
} // namespace

bool InputScript_Init(const unsigned* pFrameCounter)
{
    gFrameCounter = pFrameCounter;
    const char* keyLog = getenv("GTA2GL_KEYLOG");
    if (keyLog && *keyLog)
    {
        gKeyLog = fopen(keyLog, "w");
    }
    Parse(getenv("GTA2GL_INPUT"));
    char msg[128];
    snprintf(msg, sizeof(msg), "input_script: %u scheduled key events\n", (unsigned)gEvents.size());
    OutputDebugStringA(msg);
    return !gEvents.empty() || gKeyLog;
}

bool InputScript_Scripted()
{
    return !gEvents.empty();
}

DWORD InputScript_Due()
{
    DWORD n = 0;
    for (size_t i = gNext; i < gEvents.size() && gEvents[i].mFrame <= Frame(); i++)
    {
        n++;
    }
    return n;
}

bool InputScript_Take(LPDIDEVICEOBJECTDATA pData, DWORD size)
{
    if (!InputScript_Due())
    {
        return false;
    }
    const KeyEvent& e = gEvents[gNext++];
    memset(pData, 0, size);
    pData->dwOfs = e.mDik;
    pData->dwData = e.mDown ? 0x80 : 0;
    pData->dwTimeStamp = GetTickCount();
    pData->dwSequence = gSequence++;
    return true;
}

void InputScript_HoldKeys(BYTE* keys)
{
    for (size_t i = 0; i < gEvents.size(); i++)
    {
        const KeyEvent& e = gEvents[i];
        if (e.mDown && e.mFrame <= Frame())
        {
            // Held until its matching up event.
            for (size_t j = i + 1; j < gEvents.size(); j++)
            {
                if (!gEvents[j].mDown && gEvents[j].mDik == e.mDik)
                {
                    if (gEvents[j].mFrame > Frame())
                    {
                        keys[e.mDik & 0xFF] = 0x80;
                    }
                    break;
                }
            }
        }
    }
}

bool InputScript_Logging()
{
    return gKeyLog != 0;
}

void InputScript_LogDropped(const DIDEVICEOBJECTDATA& d)
{
    if (gKeyLog)
    {
        fprintf(gKeyLog,
                "%u frame %u dropped %02x %s\n",
                (unsigned)GetTickCount(),
                Frame(),
                (unsigned)d.dwOfs,
                (d.dwData & 0x80) ? "down" : "up");
    }
}

void InputScript_LogRead(const DIDEVICEOBJECTDATA& d, DWORD left)
{
    if (gKeyLog)
    {
        fprintf(gKeyLog,
                "%u frame %u dik %02x %s left %u\n",
                (unsigned)GetTickCount(),
                Frame(),
                (unsigned)d.dwOfs,
                (d.dwData & 0x80) ? "down" : "up",
                (unsigned)left);
        fflush(gKeyLog);
    }
}
