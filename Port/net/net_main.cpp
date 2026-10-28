// Port/net (docs/netcode.md, "Code layout"): the session's lifetime: a server, a client, a replay, or a join from the
// frontend's network page, started from the command line (cmdline.cpp) and ended when the game ends.
#include "net_main.hpp"
#include "cmdline.hpp"
#include "maps.hpp"
#include "Port/port_hooks.hpp"
#include "engine/dev/dev.hpp"
#include "session/client.hpp"
#include "session/server.hpp"
#include <SDL3/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Port/rand/rand.cpp, Port/rand/particle.cpp (not their headers: those rename rand and gRng_6F6784)
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

bool NetMenu_Join(const std::string& address, const std::string& name, std::string* error)
{
    NetMenu_Cancel();
    net::ClientConfig cc = gMenuConfig;
    net::ParseAddress(address, &cc.host, &cc.port);
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
    gPortLeaveNetworkGame = LeaveNetworkGame;
    gPortCrashHook = net::engine::DebugOnCrash; // the crash report's last: the netcode's view of the game
    net::NetOptions o;
    if (!net::ParseOptions(args, o))
    {
        return -1;
    }
    net::ServerConfig& sc = o.sc;
    net::ClientConfig& cc = o.cc;
    gPortTrafficCap = o.traffic;
    if (!o.server && o.connect.empty() && o.replay.empty())
    {
        gMenuConfig = cc; // the frontend's network page may join a server later (Port/net/menu.cpp)
        NetMenu_Install();
        return 0;
    }
    NetworkSetup(o.server);
    std::string error;
    if (o.server)
    {
        if (!o.window)
        {
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "GTA2_HEADLESS", "1", true); // what Port_IsHeadless reads
        }
        // Up to the game's 6 players until the player cap is a port setting (N6)
        if (sc.game.players < 1 || sc.game.players > 6)
        {
            fprintf(stderr, "net: --players 1-6\n");
            return -1;
        }
        if (!net::SetUpMaps(o.map, o.maps, sc))
        {
            return -1;
        }
        // The round flow (session/round_flow.cpp): on unless the server plays its own bots (an arena: tests, training)
        sc.rules.enabled = o.rounds.empty() ? sc.arena.empty() : o.rounds == "on";
        sc.rules.gameType = sc.game.gameType;
        sc.rules.fragLimit = sc.game.fragLimit;
        sc.rules.timeLimitMin = sc.game.timeLimitMin;
        sc.rules.ticksPerSecond = 1000 / (sc.game.tickMs > 0 ? sc.game.tickMs : 33);
        sc.rules.rotation = sc.maps.size() > 1;
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
    else if (!o.replay.empty())
    {
        gClient = new net::Client();
        net::engine::NetworkGame game;
        if (!gClient->StartReplay(o.replay.c_str(), &game, &error))
        {
            fprintf(stderr, "net: %s\n", error.c_str());
            return -1;
        }
        net::engine::StartNetworkGame(game);
        gTickMs = 1;
    }
    else
    {
        net::ParseAddress(o.connect, &cc.host, &cc.port);
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
