// Port (docs/netcode.md, "The rollback set"): rand() for sound_obj.cpp. By default it is the game's one stream, as in
// 10.5.exe, so replays compare exactly. With PortRandSound_Separate(true) (the netcode) sound draws from a stream
// of its own: a client that resimulates without sound then draws the same numbers as the server.
#pragma once
#include "rand.hpp"

int port_rand_sound();
void PortRandSound_Separate(bool separate);

#undef rand
#define rand port_rand_sound
