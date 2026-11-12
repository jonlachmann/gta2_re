// The game folder, and the first-start setup that fills it.
#pragma once

// Enters the game folder: $GTA2_DIR, else the current folder or the executable's when it holds the
// game's data, else the per-user folder, where the setup runs if it has no game data (or with
// forceSetup). Before the game's global constructors, which read files from it. False: quit.
bool port_PrepareGameFolder(bool forceSetup);

struct SDL_Window;

// The reG2 logo as the window's icon on Linux. On Windows windows take the exe's icon, on macOS
// the bundle's, which a window icon would replace in the Dock.
void port_SetWindowIcon(SDL_Window* pWindow);
