// Port/net (docs/netcode.md, "Running"): the netcode's command line, see cmdline.cpp.
#pragma once
#include "session/client.hpp"
#include "session/server.hpp"
#include <string>
#include <vector>

namespace net
{
struct NetOptions
{
    bool server = false; // --server
    bool window = false; // --window: a server shown in a window (else headless)
    std::string connect; // --connect host[:port]
    std::string replay; // --replay <file>
    std::string map = "downtown-6p"; // --map
    std::string maps; // --maps <mmp>,<mmp>,...
    std::string rounds; // --rounds on|off (default: on, off with --arena)
    int traffic = 16; // --traffic
    ServerConfig sc;
    ClientConfig cc;
};

// Takes the netcode's options out of <args>, the rest stays for the game; false: an error, printed
bool ParseOptions(std::vector<std::string>& args, NetOptions& o);
} // namespace net
