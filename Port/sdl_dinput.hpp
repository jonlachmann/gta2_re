// Port (docs/port.md): the DirectInput on SDL3 events, see sdl_dinput.cpp.
#pragma once
#include <SDL3/SDL.h>

void Port_DInputOnEvent(const SDL_Event& e); // with every SDL event the window loop sees (sdl_win32.cpp)
