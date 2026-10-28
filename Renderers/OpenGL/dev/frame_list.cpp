#include "frame_list.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void FrameList::InitFromEnv(const char* envName, const char* prefix)
{
    mPrefix = prefix;
    mFrames.clear();
    const char* env = getenv(envName);
    if (!env || !*env)
    {
        return;
    }
    const char* comma = strchr(env, ',');
    mDir.assign(env, comma ? comma - env : strlen(env));
#if defined(_WIN32)
    if (!mDir.empty() && mDir[0] == '/')
    {
        mDir = "Z:" + mDir;
    }
#endif
    Parse(comma ? comma + 1 : 0);
}

void FrameList::Parse(const char* list)
{
    for (const char* p = list; p && *p; p = strchr(p, ','), p = p ? p + 1 : 0)
    {
        // "a-b" is an inclusive range
        char* pEnd = NULL;
        const u32 first = (u32)strtoul(p, &pEnd, 10);
        if (pEnd == p)
        {
            continue;
        }
        const u32 last = *pEnd == '-' ? (u32)strtoul(pEnd + 1, NULL, 10) : first;
        for (u32 f = first; f <= last; f++)
        {
            mFrames.push_back(f);
        }
    }
}

bool FrameList::Contains(u32 frame) const
{
    for (size_t i = 0; i < mFrames.size(); i++)
    {
        if (mFrames[i] == frame)
        {
            return true;
        }
    }
    return false;
}

std::string FrameList::PathFor(u32 frame, const char* ext) const
{
    char name[64];
    snprintf(name, sizeof(name), "/%s_%05u%s", mPrefix.c_str(), frame, ext);
    return mDir + name;
}
