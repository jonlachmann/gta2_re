// Port/net (docs/netcode.md, "Code layout"): the command line and the session's lifetime.
//   --server [--port N] [--map <mmp>] [--players N] [--gametype 1-3] [--fraglimit N] [--timelimit min] [--tickms N]
//            (the limits end a round, and the next starts on the same map: engine/rounds.cpp)
//            [--maps <mmp>,<mmp>,...]  (a round on each in turn, the first instead of --map)
//            [--interest blocks]  (how far around a client its snapshots reach: 24)
//            [--police [stars]]  (police on, as the lobby's box, off by default as there; up to 1 star, as the game, or 1-6)
//            [--traffic N]  (the ambient traffic cars for the whole game, 16 as in the original)
//            [--packets N]  (a snapshot's packets at most, 1..8, 4 by default: a busy area's goes in parts)
//            [--arena brain,brain,...]  (bots on the server itself in the first slots, e.g. nn:best.nn,simple)
//            [--ticks N]  (prints the arena's result and ends at tick N: Scripts/port/train_bots.py)
//            [--seed N]  (the game's random state at the start: another match from the same start)
//   --connect host[:port] [--name <name>] [--bot [brain]]  (brain: simple, carhop, rammer, nn:<file>)
//   either: [--lag ms] [--jitter ms] [--loss percent]  a link conditioner on this end's sends (tests)
//   --replay <file>  a client session recorded with GTA2_NET_RECORD=<file>, played back without a server
// The map is a multiplayer map's .mmp in data/ (default downtown-6p). The game then starts that network game.
#include "net_main.hpp"
#include "Port/port_hooks.hpp" // the hooks defined here, as the game declares them
#include "session/session.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gPortTrafficCap = 16; // Source/Car.cpp, CarManager::CanAllocateCarOfKind_446930
int gPortSpawnPlayers = 1;
void (*gPortAfterPlayersHook)();
void (*gPortDrawOverlay)() = NULL; // Source/Game.cpp, Game::Draw_4B92D0
short gPortZoneRoundRobin; // Source/map_0x370.cpp, Map_0x370::GetNextZoneOfTypeRoundRobin_4DF0F0 (respawn zones)

// Port/port_rand.cpp, Port/port_rand_particle.cpp (not their headers: those rename rand and gRng_6F6784)
void PortRandSound_Separate(bool separate);
void PortRandParticle_Separate(bool separate);

namespace
{
net::Server* gServer;
net::Client* gClient;
int gTickMs = 33;
bool gTickSet;

void OnFrame(void*)
{
    if (!gTickSet && net::engine::GameRunning())
    {
        net::engine::SetTickMs(gTickMs);
        gTickSet = true;
    }
    if (gServer)
    {
        gServer->Frame();
    }
    if (gClient)
    {
        gClient->Frame();
        net::engine::GuardPeds(); // again: the snapshot this frame applied comes before the next tick
        net::engine::AnchorAttachments(); // the frame is drawn next
        net::engine::GangLog(false);
    }
}

// A client that quits, or is stopped, leaves the server at once: else its slot stays taken until the server's timeout
// (5-10 s), and a player joining meanwhile finds the server full
void LeaveServer()
{
    if (gClient)
    {
        gClient->Leave();
    }
}

void OnStopSignal(int)
{
    LeaveServer();
    _Exit(0);
}

// data/<name>.mmp: [MapFiles] GMPFile, STYFile, SCRFile
bool ReadMmp(const std::string& name, net::engine::NetworkGame& g)
{
    FILE* f = fopen(("data/" + name + ".mmp").c_str(), "r");
    if (!f)
    {
        return false;
    }
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char key[64], value[260];
        if (sscanf(line, " %63[^= ] = %259[^\r\n]", key, value) == 2)
        {
            if (!strcmp(key, "GMPFile"))
            {
                g.gmp = value;
            }
            else if (!strcmp(key, "STYFile"))
            {
                g.sty = value;
            }
            else if (!strcmp(key, "SCRFile"))
            {
                g.scr = value;
            }
        }
    }
    fclose(f);
    return !g.gmp.empty() && !g.sty.empty() && !g.scr.empty();
}
// Before any network game's start: what each end draws its random numbers from, and the log
void NetworkSetup(bool server)
{
    PortRandSound_Separate(true); // sound draws its own random numbers: a resimulation without sound draws the same (milestone S)
    PortRandParticle_Separate(true); // and particles: a resimulation makes the flames it made before
    net::engine::SetRandOwners(!server); // and each entity and server service its own: the same wherever it runs
    if (!server)
    {
        gPortDrawOverlay = net::engine::DrawOverlay; // the ping, an outage, and (GTA2_NET_MARKER) where the newest snapshot has us
    }
    setvbuf(stdout, NULL, _IOLBF, 0); // the net: lines as they happen, also when the process is killed
}

// A client the server welcomed: its game is the one the game starts next
void StartClientGame(const net::engine::NetworkGame& game)
{
    atexit(LeaveServer);
    gPortIdleHook = [] { gClient->Idle(); }; // pongs timed as they come
    signal(SIGTERM, OnStopSignal);
    signal(SIGINT, OnStopSignal);
#if !defined(_WIN32)
    signal(SIGHUP, OnStopSignal);
#endif
    net::engine::StartNetworkGame(game);
    gTickMs = game.tickMs;
    net::engine::SetFrameHook(OnFrame, NULL);
}

net::ClientConfig gMenuConfig; // the command line's client options (--name, --lag...), for a join from the frontend
net::engine::NetworkGame gMenuGame;
} // namespace

// winmain.cpp, a network game over: a client that left goes back to the network page, the netcode as before the join
int LeaveNetworkGame()
{
    if (!net::engine::LeftForMenu())
    {
        return -1;
    }
    NetMenu_SetStatus(gClient ? gClient->LeftWhy() : "");
    delete gClient; // it left the server already
    gClient = NULL;
    gPortIdleHook = NULL;
    gPortDrawOverlay = NULL;
    gTickSet = false;
    PortRandSound_Separate(false);
    PortRandParticle_Separate(false);
    net::engine::StopNetworkGame();
    printf("net: back to the frontend\n");
    return NetMenu_Page();
}
int (*gPortLeaveNetworkGame)() = LeaveNetworkGame;

bool NetMenu_Join(const std::string& address, const std::string& name, std::string* error)
{
    NetMenu_Cancel();
    net::ClientConfig cc = gMenuConfig;
    const size_t colon = address.rfind(':');
    cc.host = address.substr(0, colon);
    if (colon != std::string::npos)
    {
        cc.port = (uint16_t)atoi(address.c_str() + colon + 1);
    }
    cc.name = name;
    gClient = new net::Client();
    if (!gClient->BeginConnect(cc, error))
    {
        NetMenu_Cancel();
        return false;
    }
    printf("net: joining %s:%u as %s\n", cc.host.c_str(), cc.port, name.c_str());
    return true;
}

int NetMenu_Poll(std::string* error)
{
    if (!gClient)
    {
        return -1;
    }
    const int result = gClient->PollConnect(&gMenuGame, error);
    if (result < 0)
    {
        printf("net: %s\n", error->c_str());
        NetMenu_Cancel();
    }
    else if (result > 0)
    {
        NetworkSetup(false);
        StartClientGame(gMenuGame);
    }
    return result;
}

void NetMenu_Cancel()
{
    delete gClient; // its transport's destructor disconnects
    gClient = NULL;
}

const std::string& NetMenu_DefaultName()
{
    return gMenuConfig.name;
}

int NetMain_Init(std::vector<std::string>& args)
{
    bool server = false, window = false;
    std::string rounds; // --rounds on|off // a server runs headless (GTA2_HEADLESS) unless --window asks to watch it
    std::string connect, map = "downtown-6p", replay, maps;
    net::ServerConfig sc;
    net::ClientConfig cc;
    sc.game.players = 6;
    std::vector<std::string> rest;
    for (size_t i = 0; i < args.size(); i++)
    {
        const std::string& a = args[i];
        const bool more = i + 1 < args.size();
        if (a == "--server")
        {
            server = true;
        }
        else if (a == "--window")
        {
            window = true;
        }
        else if (a == "--connect" && more)
        {
            connect = args[++i];
        }
        else if (a == "--port" && more)
        {
            sc.port = (uint16_t)atoi(args[++i].c_str());
        }
        else if (a == "--map" && more)
        {
            map = args[++i];
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
            sc.rules.votes = (v.find("start") != std::string::npos ? 1u << net::kVoteStart : 0) |
                             (v.find("map") != std::string::npos ? 1u << net::kVoteMap : 0) | (v.find("kick") != std::string::npos ? 1u << net::kVoteKick : 0);
        }
        else if (a == "--rounds" && more)
        {
            rounds = args[++i]; // on, off (default: on, off with --arena)
        }
        else if (a == "--maps" && more)
        {
            maps = args[++i];
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
            gPortTrafficCap = atoi(args[++i].c_str());
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
            if (!net::CreateBotBrain(cc.bot))
            {
                fprintf(stderr, "net: no bot brain called %s\n", cc.bot.c_str());
                return -1;
            }
        }
        else if (a == "--replay" && more)
        {
            replay = args[++i];
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
    if (!server && connect.empty() && replay.empty())
    {
        gMenuConfig = cc; // the frontend's network page may join a server later (Port/net/menu.cpp)
        NetMenu_Install();
        return 0;
    }
    NetworkSetup(server);
    std::string error;
    if (server)
    {
        if (!window)
        {
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "GTA2_HEADLESS", "1", true); // what Port_IsHeadless reads
        }
        // The rotation: each a map's files, the first played first
        for (size_t at = 0, end = 0; !maps.empty() && end != std::string::npos; at = end + 1)
        {
            end = maps.find(',', at);
            const std::string name = maps.substr(at, end == std::string::npos ? end : end - at);
            net::engine::NetworkGame files;
            if (!ReadMmp(name, files))
            {
                fprintf(stderr, "net: --maps: no data/%s.mmp\n", name.c_str());
                return -1;
            }
            sc.maps.push_back(net::MapFiles{name, files.gmp, files.sty, files.scr});
        }
        if (!sc.maps.empty())
        {
            map = sc.maps[0].name;
        }
        // ponytail: up to the game's 6 players until the player cap is a port setting (N6)
        if (sc.game.players < 1 || sc.game.players > 6 || !ReadMmp(map, sc.game))
        {
            fprintf(stderr, "net: --players 1-6, and --map must name a data/<map>.mmp (%s)\n", map.c_str());
            return -1;
        }
        // The round flow (session/round_flow.cpp): on unless the server plays its own bots (an arena: tests, training)
        sc.rules.enabled = rounds.empty() ? sc.arena.empty() : rounds == "on";
        sc.rules.gameType = sc.game.gameType;
        sc.rules.fragLimit = sc.game.fragLimit;
        sc.rules.timeLimitMin = sc.game.timeLimitMin;
        sc.rules.ticksPerSecond = 1000 / (sc.game.tickMs > 0 ? sc.game.tickMs : 33);
        sc.rules.rotation = sc.maps.size() > 1;
        // What a vote chooses from: the rotation, else the game's multiplayer maps (those there are)
        sc.voteMaps = sc.maps;
        if (!sc.rules.rotation)
        {
            sc.voteMaps.clear();
            // ponytail: the six maps the game ships, by name; a map folder's own list when there are custom maps
            for (const char* name : {"mp1-6p", "mp2-6p", "mp5-6p", "downtown-6p", "industrial-6p", "res-6p"})
            {
                net::engine::NetworkGame files;
                if (ReadMmp(name, files))
                {
                    sc.voteMaps.push_back(net::MapFiles{name, files.gmp, files.sty, files.scr});
                }
            }
        }
        sc.currentMap = -1;
        for (size_t m = 0; m < sc.voteMaps.size(); m++)
        {
            sc.currentMap = sc.voteMaps[m].scr == sc.game.scr ? (int)m : sc.currentMap;
        }
        if (sc.currentMap < 0) // a map of its own: in the list too
        {
            sc.voteMaps.insert(sc.voteMaps.begin(), net::MapFiles{map, sc.game.gmp, sc.game.sty, sc.game.scr});
            sc.currentMap = 0;
        }
        sc.game.localPlayer = 0; // nobody plays on the server: every slot's input comes from the network
        gServer = new net::Server();
        if (!gServer->Start(sc, &error))
        {
            fprintf(stderr, "net: %s\n", error.c_str());
            return -1;
        }
        net::engine::StartNetworkGame(sc.game);
        gTickMs = sc.game.tickMs;
        gPortIdleHook = [] { gServer->Idle(); }; // pings answered at once
    }
    else if (!replay.empty())
    {
        gClient = new net::Client();
        net::engine::NetworkGame game;
        if (!gClient->StartReplay(replay.c_str(), &game, &error))
        {
            fprintf(stderr, "net: %s\n", error.c_str());
            return -1;
        }
        net::engine::StartNetworkGame(game);
        gTickMs = 1;
    }
    else
    {
        const size_t colon = connect.rfind(':');
        cc.host = connect.substr(0, colon);
        if (colon != std::string::npos)
        {
            cc.port = (uint16_t)atoi(connect.c_str() + colon + 1);
        }
        gClient = new net::Client();
        net::engine::NetworkGame game;
        if (!gClient->Connect(cc, &game, &error))
        {
            fprintf(stderr, "net: %s\n", error.c_str());
            return -1;
        }
        StartClientGame(game);
        gMenuConfig = cc;
        NetMenu_Install(); // left, it goes to the frontend: the network page there
        return 1;
    }
    net::engine::SetFrameHook(OnFrame, NULL);
    return 1;
}
