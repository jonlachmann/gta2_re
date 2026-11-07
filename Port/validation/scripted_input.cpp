#include "scripted_input.hpp"
#include <SDL3/SDL.h>

void ScriptedInput_Keys(void (*add)(DWORD dik, bool down, DWORD time))
{
    static const char* pNext = SDL_getenv("GTA2_PORT_KEYS");
    while (pNext && *pNext)
    {
        unsigned ms = 0, dik = 0, down = 0;
        if (SDL_sscanf(pNext, "%u:%u:%u", &ms, &dik, &down) != 3 || ms > SDL_GetTicks())
        {
            return;
        }
        add(dik, down != 0, ms);
        pNext = SDL_strchr(pNext, ',');
        pNext = pNext ? pNext + 1 : NULL;
    }
}

void ScriptedInput_Pad(void (*add)(DWORD ofs, DWORD data))
{
    static const char* pNext = SDL_getenv("GTA2_PORT_PAD");
    while (pNext && *pNext)
    {
        unsigned ms = 0, ofs = 0;
        int value = 0;
        if (SDL_sscanf(pNext, "%u:%u:%d", &ms, &ofs, &value) != 3 || ms > SDL_GetTicks())
        {
            return;
        }
        add(ofs, (DWORD)value);
        pNext = SDL_strchr(pNext, ',');
        pNext = pNext ? pNext + 1 : NULL;
    }
}

bool ScriptedInput_HasPad()
{
    return SDL_getenv("GTA2_PORT_PAD") != NULL;
}
