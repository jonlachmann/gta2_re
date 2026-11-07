// Port/net (docs/netcode.md, "Recordings"): a client's session as a file, to replay without a server. A client
// does nothing but react to what arrives each frame and to its own input, so a recording of exactly that (the
// server's welcome, then per frame the packets, the round trip it saw and the input it used) replays its game
// tick for tick: as fast as the game runs, and the same every time. GTA2_NET_RECORD=<file> records, --replay <file>
// plays it back. Without GTA2_NET_RECORD, a person's session is recorded anyway (NextRecordingPath), and F12 (or the key under Esc) in play
// marks a moment to look at: the client and its replay print "net: mark N at frame F" (the key is in the input).
#pragma once
#include "../core/protocol.hpp"
#include "../transport/transport.hpp"
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace net
{
// recordings/session-<n>.rec, n counting up over the sessions (recordings/next) and kept for the newest 10
std::string NextRecordingPath();

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
