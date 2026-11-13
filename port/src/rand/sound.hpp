// rand() for sound_obj.cpp (port/docs/netcode.md, "The rollback set"). By default it is the game's one stream, as in
// 10.5.exe, so replays compare exactly. With PortRand::SetSoundSeparate(true) (the netcode) sound draws from a stream
// of its own: a client that resimulates without sound then draws the same numbers as the server.
#pragma once
#include "rand.hpp"

namespace PortRand
{
int SoundDraw();
} // namespace PortRand

#undef rand
#define rand PortRand::SoundDraw
