// port/net (port/docs/netcode.md, "Running"): the multiplayer maps (data/<name>.mmp) and a server's map lists.
#pragma once
#include "engine/engine.hpp"
#include "session/server.hpp"
#include <string>

namespace net
{
// A server's maps: the first (<map>, or the rotation's first) into sc.game, the rotation (<maps>, comma-separated) and
// what a vote chooses from (the rotation, else the multiplayer maps the game ships); false: an error, printed
bool SetUpMaps(const std::string& map, const std::string& maps, ServerConfig& sc);
} // namespace net
