// Links between the Win32 compat layer, the input layer and the renderer.
#pragma once

#include <windows.h>
#include <SDL3/SDL.h>

// Resolves a game path to the host file system; out must hold PATH_MAX bytes.
const char* port_resolve_path(const char* path, char* out);

SDL_Window* port_GetSDLWindow(HWND h);
void port_SetFullscreen(HWND h, bool fullscreen);
SDL_Window* port_MainSDLWindow();

// The player's window size (gta2_registry.ini), the game resolution derived from it and from the
// desktop for full screen: the view height setting, the width following the screen's shape.
void port_GetWindowSize(int* pW, int* pH);
void port_SaveWindowSize(int w, int h);
void port_UpdateGameResolution();

// Number of Vid_FlipBuffers calls so far.
unsigned port_FrameCount();

// Scripted key presses and exit (GTA2_INPUT, GTA2_EXIT_AT), checked as the message loop pumps.
void port_InitTestInput();
void port_InjectTestInput(unsigned frame);

// Every SDL event, as the message loop pumps them.
void port_OnInputEvent(const SDL_Event& e);

#include "../../renderer/port_exports.hpp"

// Exports of a DLL the game loads by name, linked into the executable instead.
void port_RegisterDll(const char* name, const PortExport* pExports, int count);
