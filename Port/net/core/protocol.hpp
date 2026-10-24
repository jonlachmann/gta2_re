// Port/net core (docs/netcode.md, "Wire format"): constants and the messages client and server exchange. Every
// message starts with its type (one byte); readers return false on anything malformed.
#pragma once
#include <vector>
#include "bitstream.hpp"
#include <stdint.h>
#include <string>

namespace net
{
const uint32_t kProtocolVersion = 6;
const uint16_t kDefaultPort = 27920;
const size_t kMaxPayload = 1372; // the largest unreliable ENet packet that isn't fragmented at mtu 1400 (Evaluations, 7)
const uint8_t kChannelUnreliable = 0; // snapshots, commands
const uint8_t kChannelReliable = 1; // join, events, chat
const int kChannelCount = 2;
const int kMaxCmdsPerPacket = 8; // each command packet repeats the last ones, so a lost packet loses no input

enum MsgType : uint8_t
{
    kMsgHello = 1, // client -> server, reliable
    kMsgWelcome = 2, // server -> client, reliable
    kMsgReject = 3, // server -> client, reliable, then disconnect
    kMsgCommands = 4, // client -> server, unreliable
    kMsgSnapshot = 5, // server -> client, unreliable
    kMsgSnapshotPart = 6, // server -> client, unreliable: one part of a snapshot too big for one packet
};

struct Hello
{
    uint32_t version = kProtocolVersion;
    std::string name;
};

// What a client needs to start the same network game as the server
struct Welcome
{
    uint32_t version = kProtocolVersion;
    uint8_t slot = 0; // the client's player index
    uint8_t players = 0; // player slots in the game
    uint8_t gameType = 1; // 1 frags, 2 points, 3 tag (MultiplayerGameType)
    uint16_t fragLimit = 10;
    uint16_t timeLimitMin = 10;
    uint16_t tickMs = 33;
    uint8_t police = 0;
    std::string gmp, sty, scr; // file names in data/
    uint32_t serverTick = 0;
};

struct Reject
{
    std::string reason;
};

struct InputCmd
{
    uint32_t tick; // the server tick it is for
    uint32_t buttons; // the game's input bits (Source/input_bits.hpp), 22 bits
};

void WriteHello(BitWriter& w, const Hello& m);
bool ReadHello(BitReader& r, Hello& m);
void WriteWelcome(BitWriter& w, const Welcome& m);
bool ReadWelcome(BitReader& r, Welcome& m);
void WriteReject(BitWriter& w, const Reject& m);
bool ReadReject(BitReader& r, Reject& m);

// Commands: the newest snapshot tick the client has, then up to kMaxCmdsPerPacket commands for consecutive ticks,
// oldest first
void WriteCommands(BitWriter& w, uint32_t ackTick, const InputCmd* cmds, int count);
int ReadCommands(BitReader& r, uint32_t* pAckTick, InputCmd* cmds, int max); // -1 if malformed

// Snapshot header: its tick, the tick of the baseline it is a delta to (0: none), and the newest command tick
// the server has applied for this client; the delta (snapshot.hpp) follows
struct SnapshotHeader
{
    uint32_t tick;
    uint32_t baseTick;
    uint32_t lastCmdTick;
};
// A snapshot message bigger than one packet goes in parts (up to kMaxParts): its tick, the part's index and how many,
// then a piece of the message's bytes. A client puts them together, in order, once it has all of them
const int kMaxParts = 8;
const size_t kPartHeader = 8; // the most a part's header takes, in bytes
void WriteSnapshotParts(const std::vector<uint8_t>& message, uint32_t tick, std::vector<std::vector<uint8_t>>& parts);
bool ReadSnapshotPart(BitReader& r, uint32_t* pTick, int* pIndex, int* pCount, std::vector<uint8_t>& piece);
void WriteSnapshotHeader(BitWriter& w, const SnapshotHeader& h);
bool ReadSnapshotHeader(BitReader& r, SnapshotHeader& h);
} // namespace net
