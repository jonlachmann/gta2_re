// Port/net (docs/netcode.md, "Test fixtures"): a client's session as a file, to replay without a server. A client
// does nothing but react to what arrives each frame and to its own input, so a recording of exactly that (the
// server's welcome, then per frame the packets, the round trip it saw and the input it used) replays its game
// tick for tick: as fast as the game runs, and the same every time. GTA2_NET_RECORD=<file> records, --replay <file>
// plays it back.
#pragma once
#include "../core/protocol.hpp"
#include "../transport/transport.hpp"
#include <stdint.h>
#include <stdio.h>
#include <vector>

namespace net
{
class ClientRecorder
{
  public:
    ~ClientRecorder();
    bool Open(const char* path, const Welcome& welcome);
    void Frame(uint32_t frame, uint32_t rttMs, uint32_t input, const std::vector<TransportEvent>& events);

  private:
    FILE* mFile = nullptr;
};

class ClientReplay
{
  public:
    ~ClientReplay();
    bool Open(const char* path, Welcome* pWelcome);
    // The next frame's record; false at the end of the recording
    bool Frame(uint32_t* pFrame, uint32_t* pRttMs, uint32_t* pInput, std::vector<TransportEvent>& events);

  private:
    FILE* mFile = nullptr;
};
} // namespace net
