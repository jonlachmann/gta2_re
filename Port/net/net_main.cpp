// Port/net (docs/netcode.md, "Code layout"): the command line and the session's lifetime.
//   --server [--port N] [--map <mmp>] [--players N] [--gametype 1-3] [--fraglimit N] [--timelimit min] [--tickms N]
//            [--traffic N]  (the ambient traffic cars for the whole game, 16 as in the original)
//   --connect host[:port] [--name <name>] [--bot [brain]]  (brain: simple, carhop, rammer)
//   either: [--lag ms] [--jitter ms] [--loss percent]  a link conditioner on this end's sends (tests)
//   --replay <file>  a client session recorded with GTA2_NET_RECORD=<file>, played back without a server
// The map is a multiplayer map's .mmp in data/ (default downtown-6p). The game then starts that network game.
#include "net_main.hpp"
#include "session/session.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gPortTrafficCap = 16; // Source/Car.cpp, CarManager::CanAllocateCarOfKind_446930
void (*gPortDrawOverlay)() = NULL; // Source/Game.cpp, Game::Draw_4B92D0
short gPortZoneRoundRobin; // Source/map_0x370.cpp, Map_0x370::GetNextZoneOfTypeRoundRobin_4DF0F0 (respawn zones)

void PortRandSound_Separate(bool separate);
void PortRandParticle_Separate(bool separate); // Port/port_rand_particle.h (it renames gRng_6F6784) // Port/port_rand.cpp (not its header: that one renames rand)

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
    }
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
} // namespace

int NetMain_Init(std::vector<std::string>& args)
{
    bool server = false;
    std::string connect, map = "downtown-6p", name = "player", replay;
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
        else if (a == "--traffic" && more)
        {
            gPortTrafficCap = atoi(args[++i].c_str());
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
        return 0;
    }
    PortRandSound_Separate(true); // sound draws its own random numbers: a resimulation without sound draws the same (milestone S)
    PortRandParticle_Separate(true); // and particles: a resimulation makes the flames it made before
    if (getenv("GTA2_NET_MARKER"))
    {
        gPortDrawOverlay = net::engine::DrawOverlay; // where the newest snapshot has us, over the frame
    }
    setvbuf(stdout, NULL, _IOLBF, 0); // the net: lines as they happen, also when the process is killed
    std::string error;
    if (server)
    {
        // ponytail: up to the game's 6 players until the player cap is a port setting (N6)
        if (sc.game.players < 1 || sc.game.players > 6 || !ReadMmp(map, sc.game))
        {
            fprintf(stderr, "net: --players 1-6, and --map must name a data/<map>.mmp (%s)\n", map.c_str());
            return -1;
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
        net::engine::StartNetworkGame(game);
        gTickMs = game.tickMs;
    }
    net::engine::SetFrameHook(OnFrame, NULL);
    return 1;
}
