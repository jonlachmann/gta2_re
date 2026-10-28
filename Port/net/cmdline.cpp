// Port/net (docs/netcode.md, "Running"): the netcode's command line.
//   --server [--port N] [--map <mmp>] [--players N] [--gametype 1-3] [--fraglimit N] [--timelimit min] [--tickms N]
//            (the limits end a round: the round flow, session/round_flow.cpp)
//            [--minplayers N] [--warmup s] [--results s] [--votes start,map,kick] [--rounds on|off]  (the round flow)
//            [--window]  (a server is headless unless it is watched in a window)
//            [--maps <mmp>,<mmp>,...]  (a round on each in turn, the first instead of --map)
//            [--interest blocks]  (how far around a client its snapshots reach: 24)
//            [--police [stars]]  (police on, as the lobby's box, off by default as there; up to 1 star, as the game, or 1-6)
//            [--traffic N]  (the ambient traffic cars for the whole game, 16 as in the original)
//            [--packets N]  (a snapshot's packets at most, 1..8, 4 by default: a busy area's goes in parts)
//            [--arena brain,brain,...]  (bots on the server itself in the first slots, e.g. nn:best.nn,simple)
//            [--ticks N]  (prints the arena's result and ends at tick N: Scripts/port/bots/rl_bots.py)
//            [--seed N]  (the game's random state at the start: another match from the same start)
//   --connect host[:port] [--name <name>] [--bot [brain]]  (brain: simple, carhop, rammer, soldier, hunter, nn:<file>)
//   either: [--lag ms] [--jitter ms] [--loss percent]  a link conditioner on this end's sends (tests)
//   --replay <file>  a client session recorded with GTA2_NET_RECORD=<file>, played back without a server
// The map is a multiplayer map's .mmp in data/ (default downtown-6p). The game then starts that network game.
#include "cmdline.hpp"
#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

bool net::ParseOptions(std::vector<std::string>& args, NetOptions& o)
{
    ServerConfig& sc = o.sc;
    ClientConfig& cc = o.cc;
    sc.game.players = 6;
    std::vector<std::string> rest;
    for (size_t i = 0; i < args.size(); i++)
    {
        const std::string& a = args[i];
        const bool more = i + 1 < args.size();
        if (a == "--server")
        {
            o.server = true;
        }
        else if (a == "--window")
        {
            o.window = true;
        }
        else if (a == "--connect" && more)
        {
            o.connect = args[++i];
        }
        else if (a == "--port" && more)
        {
            sc.port = (uint16_t)atoi(args[++i].c_str());
        }
        else if (a == "--map" && more)
        {
            o.map = args[++i];
        }
        else if (a == "--minplayers" && more)
        {
            sc.rules.minPlayers = atoi(args[++i].c_str());
        }
        else if (a == "--warmup" && more)
        {
            sc.rules.warmupSec = atoi(args[++i].c_str());
        }
        else if (a == "--results" && more)
        {
            sc.rules.resultsSec = atoi(args[++i].c_str());
        }
        else if (a == "--votes" && more)
        {
            const std::string v = args[++i];
            sc.rules.votes = (v.find("start") != std::string::npos ? 1u << kVoteStart : 0) |
                             (v.find("map") != std::string::npos ? 1u << kVoteMap : 0) | (v.find("kick") != std::string::npos ? 1u << kVoteKick : 0);
        }
        else if (a == "--rounds" && more)
        {
            o.rounds = args[++i]; // on, off (default: on, off with --arena)
        }
        else if (a == "--maps" && more)
        {
            o.maps = args[++i];
        }
        else if (a == "--players" && more)
        {
            sc.game.players = atoi(args[++i].c_str());
        }
        else if (a == "--gametype" && more)
        {
            sc.game.gameType = atoi(args[++i].c_str());
        }
        else if (a == "--fraglimit" && more)
        {
            sc.game.fragLimit = atoi(args[++i].c_str());
        }
        else if (a == "--timelimit" && more)
        {
            sc.game.timeLimitMin = atoi(args[++i].c_str());
        }
        else if (a == "--tickms" && more)
        {
            sc.game.tickMs = atoi(args[++i].c_str());
        }
        else if (a == "--interest" && more)
        {
            sc.interestRadius = (float)atof(args[++i].c_str());
        }
        else if (a == "--traffic" && more)
        {
            o.traffic = atoi(args[++i].c_str());
        }
        else if (a == "--police")
        {
            sc.game.police = 1; // the lobby's "police" box (off by default there too)
            if (more && isdigit((unsigned char)args[i + 1][0]))
            {
                sc.game.police = std::max(1, std::min(6, atoi(args[++i].c_str()))); // the most stars (the game's own: 1)
            }
        }
        else if (a == "--packets" && more)
        {
            sc.packets = atoi(args[++i].c_str());
        }
        else if (a == "--arena" && more)
        {
            for (size_t at = 0, end = 0; end != std::string::npos; at = end + 1)
            {
                end = args[i + 1].find(',', at);
                sc.arena.push_back(args[i + 1].substr(at, end == std::string::npos ? end : end - at));
            }
            i++;
        }
        else if (a == "--seed" && more)
        {
            sc.seed = (uint32_t)strtoul(args[++i].c_str(), NULL, 10);
        }
        else if (a == "--ticks" && more)
        {
            sc.endTick = (uint32_t)atoi(args[++i].c_str());
        }
        else if (a == "--name" && more)
        {
            cc.name = args[++i];
        }
        else if (a == "--bot")
        {
            cc.bot = more && args[i + 1].compare(0, 2, "--") != 0 ? args[++i] : "simple"; // --bot [brain]
            if (!CreateBotBrain(cc.bot))
            {
                fprintf(stderr, "net: no bot brain called %s\n", cc.bot.c_str());
                return false;
            }
        }
        else if (a == "--replay" && more)
        {
            o.replay = args[++i];
        }
        else if (a == "--lag" && more)
        {
            sc.lagMs = cc.lagMs = atoi(args[++i].c_str());
        }
        else if (a == "--jitter" && more)
        {
            sc.jitterMs = cc.jitterMs = atoi(args[++i].c_str());
        }
        else if (a == "--loss" && more)
        {
            sc.lossPercent = cc.lossPercent = atoi(args[++i].c_str());
        }
        else
        {
            rest.push_back(a);
        }
    }
    args = rest;
    return true;
}
