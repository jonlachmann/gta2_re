// The 3D provider (miles3d.cpp) as the 2D mixer (miles.cpp) drives it.
#pragma once

const int Miles3D_kRate = 44100;

// Adds the 3D samples to an interleaved stereo float mix; false when no provider is open.
bool Miles3D_Render(float* pMix, int frames);

void Miles3D_SetMasterVolume(S32 volume);
