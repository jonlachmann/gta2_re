// Test tools driven by environment variables, for runs nobody watches:
//   GTA2_INPUT=<frame>:<key>[:<frames held>],...   SDL key names ("Return", "Down", "Escape", "A"),
//                                                   "Alt+" for the Alt modifier ("Alt+Return")
//   GTA2_EXIT_AT=<frame>                            quit at that frame
// Frames count Vid_FlipBuffers calls.
#include "port_platform.hpp"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{
struct KeyEvent
{
    unsigned mFrame;
    SDL_Scancode mScancode;
    SDL_Keymod mMod;
    bool mDown;
};

std::vector<KeyEvent>& Events()
{
    static std::vector<KeyEvent> sEvents;
    return sEvents;
}

unsigned sExitAt;
bool sLoaded;

void Load()
{
    sLoaded = true;
    if (const char* exitAt = getenv("GTA2_EXIT_AT"))
    {
        sExitAt = (unsigned)atoi(exitAt);
    }
    const char* spec = getenv("GTA2_INPUT");
    if (!spec)
    {
        return;
    }
    std::string s(spec);
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t end = s.find(',', pos);
        if (end == std::string::npos)
        {
            end = s.size();
        }
        const std::string item = s.substr(pos, end - pos);
        pos = end + 1;
        const size_t c1 = item.find(':');
        if (c1 == std::string::npos)
        {
            continue;
        }
        const size_t c2 = item.find(':', c1 + 1);
        const unsigned frame = (unsigned)atoi(item.c_str());
        std::string key = item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
        SDL_Keymod mod = SDL_KMOD_NONE;
        if (key.compare(0, 4, "Alt+") == 0)
        {
            mod = SDL_KMOD_LALT;
            key = key.substr(4);
        }
        const unsigned held = c2 == std::string::npos ? 2 : (unsigned)atoi(item.c_str() + c2 + 1);
        const SDL_Scancode sc = SDL_GetScancodeFromName(key.c_str());
        if (sc == SDL_SCANCODE_UNKNOWN)
        {
            SDL_Log("GTA2_INPUT: unknown key %s", key.c_str());
            continue;
        }
        Events().push_back(KeyEvent{frame, sc, mod, true});
        Events().push_back(KeyEvent{frame + held, sc, mod, false});
    }
}
} // namespace

void port_InjectTestInput(unsigned frame)
{
    if (!sLoaded)
    {
        Load();
    }
    for (auto it = Events().begin(); it != Events().end();)
    {
        if (it->mFrame > frame)
        {
            ++it;
            continue;
        }
        SDL_Event e = {};
        e.type = it->mDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = it->mScancode;
        e.key.key = SDL_GetKeyFromScancode(it->mScancode, SDL_KMOD_NONE, false);
        e.key.mod = it->mMod;
        e.key.down = it->mDown;
        SDL_PushEvent(&e);
        SDL_Log("test input: frame %u %s %s", frame, SDL_GetScancodeName(it->mScancode), it->mDown ? "down" : "up");
        it = Events().erase(it);
    }
    if (sExitAt && frame >= sExitAt)
    {
        sExitAt = 0;
        SDL_Event e = {};
        e.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&e);
    }
}
