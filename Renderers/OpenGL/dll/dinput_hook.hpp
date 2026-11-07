// Hook on the game's DirectInput keyboard: drops NumLock and Scroll Lock events (wine workaround), and the
// scripted keys of dev/input_script.cpp.
#pragma once

void InstallDInputHook(const unsigned* pFrameCounter);
