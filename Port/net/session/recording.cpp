// Port/net: see replay.hpp. The file: "GTA2NETR", a version, the welcome as the protocol writes it (size first),
// then per frame: frame, round trip, input, event count, and per event its type, channel, size and bytes.
// Little-endian 32-bit numbers throughout.
#include "recording.hpp"
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace
{
const char kMagic[8] = {'G', 'T', 'A', '2', 'N', 'E', 'T', 'R'};
const uint32_t kVersion = 1;

void Put(FILE* f, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

bool Get(FILE* f, uint32_t* v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4)
    {
        return false;
    }
    *v = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}
} // namespace

std::string net::NextRecordingPath()
{
#if defined(_WIN32)
    _mkdir("recordings");
#else
    mkdir("recordings", 0755);
#endif
    unsigned next = 0;
    if (FILE* f = fopen("recordings/next", "r"))
    {
        if (fscanf(f, "%u", &next) != 1)
        {
            next = 0;
        }
        fclose(f);
    }
    if (FILE* f = fopen("recordings/next", "w"))
    {
        fprintf(f, "%u\n", next + 1);
        fclose(f);
    }
    char path[64];
    snprintf(path, sizeof(path), "recordings/session-%u.rec", next % 10);
    return path;
}

net::ClientRecorder::~ClientRecorder()
{
    if (mFile)
    {
        fclose(mFile);
    }
}

bool net::ClientRecorder::Open(const char* path, const Welcome& welcome)
{
    mFile = fopen(path, "wb");
    if (!mFile)
    {
        return false;
    }
    fwrite(kMagic, 1, sizeof(kMagic), mFile);
    Put(mFile, kVersion);
    BitWriter w;
    WriteWelcome(w, welcome);
    Put(mFile, (uint32_t)w.ByteCount());
    fwrite(w.Data().data(), 1, w.ByteCount(), mFile);
    return true;
}

void net::ClientRecorder::Frame(uint32_t frame, uint32_t rttMs, uint32_t input, const std::vector<TransportEvent>& events)
{
    if (!mFile)
    {
        return;
    }
    Put(mFile, frame);
    Put(mFile, rttMs);
    Put(mFile, input);
    Put(mFile, (uint32_t)events.size());
    for (size_t i = 0; i < events.size(); i++)
    {
        Put(mFile, (uint32_t)events[i].type);
        Put(mFile, events[i].channel);
        Put(mFile, (uint32_t)events[i].data.size());
        fwrite(events[i].data.data(), 1, events[i].data.size(), mFile);
    }
    fflush(mFile); // a crash keeps everything up to it
}

net::ClientReplay::~ClientReplay()
{
    if (mFile)
    {
        fclose(mFile);
    }
}

bool net::ClientReplay::Open(const char* path, Welcome* pWelcome)
{
    mFile = fopen(path, "rb");
    char magic[8];
    uint32_t version = 0, size = 0;
    if (!mFile || fread(magic, 1, 8, mFile) != 8 || memcmp(magic, kMagic, 8) || !Get(mFile, &version) || version != kVersion ||
        !Get(mFile, &size) || size > 4096)
    {
        return false;
    }
    std::vector<uint8_t> bytes(size);
    if (fread(bytes.data(), 1, size, mFile) != size)
    {
        return false;
    }
    BitReader r(bytes.data(), bytes.size());
    return r.Bits(8) == kMsgWelcome && ReadWelcome(r, *pWelcome);
}

bool net::ClientReplay::Frame(uint32_t* pFrame, uint32_t* pRttMs, uint32_t* pInput, std::vector<TransportEvent>& events)
{
    events.clear();
    uint32_t count = 0;
    if (!mFile || !Get(mFile, pFrame) || !Get(mFile, pRttMs) || !Get(mFile, pInput) || !Get(mFile, &count))
    {
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t type = 0, channel = 0, size = 0;
        if (!Get(mFile, &type) || !Get(mFile, &channel) || !Get(mFile, &size) || size > (1u << 20))
        {
            return false;
        }
        TransportEvent e;
        e.type = (TransportEvent::Type)type;
        e.channel = (uint8_t)channel;
        e.data.resize(size);
        if (fread(e.data.data(), 1, size, mFile) != size)
        {
            return false;
        }
        events.push_back(e);
    }
    return true;
}
