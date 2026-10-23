// Port/net transport (docs/netcode.md, "Code layout"): hosts, peers and channels, independent of the library
// underneath (enet_transport.cpp). A server host has many peers, a client host one, the server.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace net
{
struct TransportEvent
{
    enum Type
    {
        kNone,
        kConnect,
        kDisconnect,
        kReceive,
    };
    Type type = kNone;
    uint32_t peer = 0; // stable while connected; reused after a disconnect
    uint8_t channel = 0;
    std::vector<uint8_t> data;
};

class Transport
{
  public:
    virtual ~Transport()
    {
    }
    // The next event, without waiting; false when there is none
    virtual bool Poll(TransportEvent& event) = 0;
    // Queued until Flush; false if the peer is gone or the packet is too big for an unreliable send
    virtual bool Send(uint32_t peer, uint8_t channel, const void* data, size_t size, bool reliable) = 0;
    virtual void Flush() = 0;
    virtual void Disconnect(uint32_t peer) = 0;
    virtual uint32_t RoundTripMs(uint32_t peer) const = 0;
};

// Listens on <port>; nullptr and <error> on failure
Transport* CreateServerTransport(uint16_t port, uint32_t maxPeers, std::string* error);
// Starts connecting to host:port (a kConnect event follows, or kDisconnect on failure); its peer is 0
Transport* CreateClientTransport(const char* host, uint16_t port, std::string* error);
} // namespace net
