// Port/net (docs/netcode.md, "Running"): the multiplayer maps (data/<name>.mmp) and a server's map lists.
#pragma once
#include "engine/engine.hpp"
#include "session/server.hpp"
#include <stdint.h>
#include <string>

namespace net
{
// data/<name>.mmp's files into <g> (gmp, sty, scr); false: no such map
bool ReadMmp(const std::string& name, engine::NetworkGame& g);
// A server's maps: the first (<map>, or the rotation's first) into sc.game, the rotation (<maps>, comma-separated) and
// what a vote chooses from (the rotation, else the multiplayer maps the game ships); false: an error, printed
bool SetUpMaps(const std::string& map, const std::string& maps, ServerConfig& sc);
// "host[:port]" into <host>, and <port> when it has one
void ParseAddress(const std::string& address, std::string* host, uint16_t* port);
} // namespace net
