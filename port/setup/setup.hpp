// The game folder, and the first-start setup that fills it.
#pragma once

// Enters the game folder: $GTA2_DIR, else the current folder or the executable's when it holds the
// game's data, else the per-user folder, where the setup runs if it has no game data (or with
// forceSetup). Before the game's global constructors, which read files from it. False: quit.
bool port_PrepareGameFolder(bool forceSetup);
