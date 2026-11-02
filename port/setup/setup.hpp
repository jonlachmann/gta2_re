// The game folder, and the first-run setup that fills it.
#pragma once

// Enters the game folder: $GTA2_DIR, else the current folder or the executable's when it holds the
// game's data (data/fstyle.sty), else the per-user folder. Called before the game's global
// constructors, which read files from it.
void port_EnterGameFolder();

// In the per-user folder without the game's data, or with force: the setup screens. False: quit.
bool port_RunSetup(bool force);
