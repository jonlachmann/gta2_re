// Port/net transport test (Scripts/port/net_tests.sh): a server and a client in one process over loopback.
#include "../core/protocol.hpp"
#include "../transport/transport.hpp"
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

#define CHECK(c) \
    do \
    { \
        if (!(c)) \
        { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            exit(1); \
        } \
    } while (0)

namespace
{
// Polls both until <want> arrives on <on> (or 3 s pass)
bool WaitFor(net::Transport* a, net::Transport* b, net::Transport* on, net::TransportEvent::Type want, net::TransportEvent* pOut)
{
    for (int i = 0; i < 3000; i++)
    {
        net::TransportEvent e;
        net::Transport* both[2] = {a, b};
        for (int k = 0; k < 2; k++)
        {
            while (both[k]->Poll(e))
            {
                if (both[k] == on && e.type == want)
                {
                    *pOut = e;
                    return true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

int main()
{
    std::string error;
    const uint16_t port = 27999;
    net::Transport* server = net::CreateServerTransport(port, 4, &error);
    CHECK(server);
    net::Transport* client = net::CreateClientTransport("127.0.0.1", port, &error);
    CHECK(client);
    net::TransportEvent e;
    CHECK(WaitFor(server, client, client, net::TransportEvent::kConnect, &e) && e.peer == 0);
    net::TransportEvent se;
    CHECK(WaitFor(server, client, server, net::TransportEvent::kConnect, &se));
    const uint32_t clientOnServer = se.peer;

    const char hello[] = "hello server";
    CHECK(client->Send(0, net::kChannelReliable, hello, sizeof(hello), true));
    client->Flush();
    CHECK(WaitFor(server, client, server, net::TransportEvent::kReceive, &se));
    CHECK(se.channel == net::kChannelReliable && se.data.size() == sizeof(hello) && !memcmp(se.data.data(), hello, sizeof(hello)));

    std::vector<uint8_t> snap(net::kMaxPayload, 0x5A);
    CHECK(server->Send(clientOnServer, net::kChannelUnreliable, snap.data(), snap.size(), false));
    std::vector<uint8_t> tooBig(net::kMaxPayload + 1, 1);
    CHECK(!server->Send(clientOnServer, net::kChannelUnreliable, tooBig.data(), tooBig.size(), false)); // would fragment
    server->Flush();
    CHECK(WaitFor(server, client, client, net::TransportEvent::kReceive, &e));
    CHECK(e.channel == net::kChannelUnreliable && e.data == snap);
    CHECK(server->RoundTripMs(clientOnServer) < 1000);

    client->Disconnect(0);
    client->Flush();
    CHECK(WaitFor(server, client, server, net::TransportEvent::kDisconnect, &se) && se.peer == clientOnServer);
    delete client;
    delete server;

    // Nobody listening: the attempt ends in a disconnect
    net::Transport* lone = net::CreateClientTransport("127.0.0.1", port + 1, &error);
    CHECK(lone);
    bool failed = false;
    for (int i = 0; i < 15000 && !failed; i++)
    {
        while (lone->Poll(e))
        {
            failed |= e.type == net::TransportEvent::kDisconnect;
            CHECK(e.type != net::TransportEvent::kConnect);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(failed);
    delete lone;
    printf("net transport tests passed\n");
    return 0;
}
