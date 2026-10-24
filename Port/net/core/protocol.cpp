// Port/net core: see protocol.hpp.
#include "protocol.hpp"

namespace
{
const size_t kMaxString = 255;

void WriteString(net::BitWriter& w, const std::string& s)
{
    const size_t n = s.size() < kMaxString ? s.size() : kMaxString;
    w.VarU((uint32_t)n);
    w.Bytes(s.data(), n);
}

bool ReadString(net::BitReader& r, std::string& s)
{
    const uint32_t n = r.VarU();
    if (n > kMaxString)
    {
        return false;
    }
    s.resize(n);
    return r.Bytes(&s[0], n);
}
} // namespace

void net::WriteHello(BitWriter& w, const Hello& m)
{
    w.Bits(kMsgHello, 8);
    w.VarU(m.version);
    WriteString(w, m.name);
}

bool net::ReadHello(BitReader& r, Hello& m)
{
    m.version = r.VarU();
    return ReadString(r, m.name) && !r.Overflow();
}

void net::WriteWelcome(BitWriter& w, const Welcome& m)
{
    w.Bits(kMsgWelcome, 8);
    w.VarU(m.version);
    w.Bits(m.slot, 8);
    w.Bits(m.players, 8);
    w.Bits(m.gameType, 8);
    w.VarU(m.fragLimit);
    w.VarU(m.timeLimitMin);
    w.VarU(m.tickMs);
    w.Bits(m.police, 8);
    WriteString(w, m.gmp);
    WriteString(w, m.sty);
    WriteString(w, m.scr);
    w.VarU(m.serverTick);
}

bool net::ReadWelcome(BitReader& r, Welcome& m)
{
    m.version = r.VarU();
    m.slot = (uint8_t)r.Bits(8);
    m.players = (uint8_t)r.Bits(8);
    m.gameType = (uint8_t)r.Bits(8);
    m.fragLimit = (uint16_t)r.VarU();
    m.timeLimitMin = (uint16_t)r.VarU();
    m.tickMs = (uint16_t)r.VarU();
    m.police = (uint8_t)r.Bits(8);
    const bool ok = ReadString(r, m.gmp) && ReadString(r, m.sty) && ReadString(r, m.scr);
    m.serverTick = r.VarU();
    return ok && !r.Overflow() && m.slot < m.players;
}

void net::WriteReject(BitWriter& w, const Reject& m)
{
    w.Bits(kMsgReject, 8);
    WriteString(w, m.reason);
}

bool net::ReadReject(BitReader& r, Reject& m)
{
    return ReadString(r, m.reason) && !r.Overflow();
}

void net::WriteMapChange(BitWriter& w, const MapChange& m)
{
    w.Bits(kMsgMapChange, 8);
    WriteString(w, m.gmp);
    WriteString(w, m.sty);
    WriteString(w, m.scr);
    w.Bits(m.game, 16);
}

bool net::ReadMapChange(BitReader& r, MapChange& m)
{
    const bool ok = ReadString(r, m.gmp) && ReadString(r, m.sty) && ReadString(r, m.scr);
    m.game = (uint16_t)r.Bits(16);
    return ok && !r.Overflow();
}

void net::WriteCommands(BitWriter& w, uint32_t ackTick, const InputCmd* cmds, int count)
{
    w.Bits(kMsgCommands, 8);
    w.VarU(ackTick);
    w.Bits((uint32_t)count, 4);
    if (!count)
    {
        return;
    }
    w.VarU(cmds[count - 1].tick); // the newest; the others are the ticks before it
    for (int i = 0; i < count; i++)
    {
        const bool same = i && cmds[i].buttons == cmds[i - 1].buttons;
        w.Bool(same);
        if (!same)
        {
            w.Bits(cmds[i].buttons, 22);
        }
    }
}

int net::ReadCommands(BitReader& r, uint32_t* pAckTick, InputCmd* cmds, int max)
{
    *pAckTick = r.VarU();
    const int count = (int)r.Bits(4);
    if (count > max || count > kMaxCmdsPerPacket)
    {
        return -1;
    }
    if (!count)
    {
        return r.Overflow() ? -1 : 0;
    }
    const uint32_t last = r.VarU();
    if (last < (uint32_t)count)
    {
        return -1;
    }
    for (int i = 0; i < count; i++)
    {
        cmds[i].tick = last - (uint32_t)(count - 1 - i);
        cmds[i].buttons = r.Bool() && i ? cmds[i - 1].buttons : r.Bits(22);
    }
    return r.Overflow() ? -1 : count;
}

void net::WriteSnapshotHeader(BitWriter& w, const SnapshotHeader& h)
{
    w.Bits(kMsgSnapshot, 8);
    w.VarU(h.tick);
    w.VarU(h.tick - h.baseTick); // 0..: how far back the baseline is (0: none)
    w.VarU(h.lastCmdTick);
}

bool net::ReadSnapshotHeader(BitReader& r, SnapshotHeader& h)
{
    h.tick = r.VarU();
    const uint32_t back = r.VarU();
    h.lastCmdTick = r.VarU();
    if (r.Overflow() || back > h.tick)
    {
        return false;
    }
    h.baseTick = h.tick - back;
    return true;
}

void net::WriteSnapshotParts(const std::vector<uint8_t>& message, uint32_t tick, std::vector<std::vector<uint8_t>>& parts)
{
    parts.clear();
    const size_t piece = kMaxPayload - kPartHeader;
    const int count = (int)((message.size() + piece - 1) / piece);
    for (int i = 0; i < count; i++)
    {
        BitWriter w;
        w.Bits(kMsgSnapshotPart, 8);
        w.VarU(tick);
        w.Bits((uint32_t)i, 3);
        w.Bits((uint32_t)(count - 1), 3);
        const size_t at = (size_t)i * piece, n = message.size() - at < piece ? message.size() - at : piece;
        w.Bytes(&message[at], n);
        parts.push_back(w.Data());
        parts.back().resize(w.ByteCount());
    }
}

bool net::ReadSnapshotPart(BitReader& r, uint32_t* pTick, int* pIndex, int* pCount, std::vector<uint8_t>& piece)
{
    *pTick = r.VarU();
    *pIndex = (int)r.Bits(3);
    *pCount = (int)r.Bits(3) + 1;
    if (r.Overflow() || *pIndex >= *pCount)
    {
        return false;
    }
    // The rest, byte-aligned
    const size_t left = r.BitsLeft() / 8;
    piece.resize(left);
    return left > 0 && r.Bytes(piece.data(), left);
}
