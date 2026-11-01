// Miles Sound System (the AIL_* subset GTA2 uses) on SDL3 audio: 2D samples from the game's sound
// banks, and WAV streams (radio stations, voices) decoded while they play, PCM or IMA ADPCM.
// No 3D provider is offered, so the game mixes everything in 2D.
#include "types.hpp"
#include "3rdParty/mss.h"
#include "port_platform.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mutex>
#include <vector>

#undef fopen

namespace
{
const int kOutRate = 44100;

std::recursive_mutex& Lock()
{
    static std::recursive_mutex sLock;
    return sLock;
}

struct Sample
{
    const u8* mData = NULL;
    u32 mLen = 0;
    S32 mFormat = DIG_F_MONO_16;
    bool mSigned8 = false;
    S32 mRate = 22050;
    S32 mVolume = 127;
    S32 mPan = 64;
    S32 mLoopCount = 1;
    S32 mLoopStart = 0;
    S32 mLoopEnd = -1;
    U32 mStatus = SMP_DONE;
    double mPos = 0; // in frames
};

// A WAV file read block by block on the audio thread.
struct Stream
{
    FILE* mFile = NULL;
    int mFormatTag = 1;
    int mChannels = 1;
    int mRate = 22050;
    int mBits = 16;
    int mBlockAlign = 0;
    int mFramesPerBlock = 0;
    long mDataStart = 0;
    u32 mDataSize = 0;
    u32 mTotalFrames = 0;
    S32 mVolume = 127;
    S32 mPlayRate = 0;
    S32 mLoopCount = 1;
    U32 mStatus = SMP_DONE;

    std::vector<s16> mBuf; // decoded frames, interleaved
    u32 mBufFirst = 0;     // frame index of mBuf[0]
    double mPos = 0;       // frame position
};

struct Driver
{
    SDL_AudioStream* mDevice = NULL;
    S32 mMasterVolume = 127;
    std::vector<Sample*> mSamples;
    std::vector<Stream*> mStreams;
};

Driver* sDriver;

const int kImaSteps[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
    88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int kImaIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

s16 ImaNibble(int nibble, int& predictor, int& index)
{
    const int step = kImaSteps[index];
    int diff = step >> 3;
    if (nibble & 1)
    {
        diff += step >> 2;
    }
    if (nibble & 2)
    {
        diff += step >> 1;
    }
    if (nibble & 4)
    {
        diff += step;
    }
    predictor += (nibble & 8) ? -diff : diff;
    predictor = predictor < -32768 ? -32768 : (predictor > 32767 ? 32767 : predictor);
    index += kImaIndex[nibble];
    index = index < 0 ? 0 : (index > 88 ? 88 : index);
    return (s16)predictor;
}

u16 Rd16(const u8* p)
{
    return (u16)(p[0] | (p[1] << 8));
}

u32 Rd32(const u8* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

bool OpenWav(Stream* s, const char* path)
{
    char real[PATH_MAX];
    s->mFile = fopen(port_resolve_path(path, real), "rb");
    if (!s->mFile)
    {
        return false;
    }
    u8 hdr[12];
    if (fread(hdr, 1, 12, s->mFile) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4))
    {
        return false;
    }
    bool haveFmt = false;
    u8 chunk[8];
    while (fread(chunk, 1, 8, s->mFile) == 8)
    {
        const u32 size = Rd32(chunk + 4);
        if (!memcmp(chunk, "fmt ", 4))
        {
            u8 fmt[64] = {};
            const u32 n = size < sizeof(fmt) ? size : sizeof(fmt);
            if (fread(fmt, 1, n, s->mFile) != n)
            {
                return false;
            }
            fseek(s->mFile, (long)(size - n + (size & 1)), SEEK_CUR);
            s->mFormatTag = Rd16(fmt);
            s->mChannels = Rd16(fmt + 2);
            s->mRate = (int)Rd32(fmt + 4);
            s->mBlockAlign = Rd16(fmt + 12);
            s->mBits = Rd16(fmt + 14);
            s->mFramesPerBlock = size >= 20 ? Rd16(fmt + 18) : 0;
            haveFmt = true;
        }
        else if (!memcmp(chunk, "data", 4))
        {
            s->mDataStart = ftell(s->mFile);
            s->mDataSize = size;
            break;
        }
        else
        {
            fseek(s->mFile, (long)(size + (size & 1)), SEEK_CUR);
        }
    }
    if (!haveFmt || !s->mDataStart || s->mChannels < 1 || s->mChannels > 2)
    {
        return false;
    }
    if (s->mFormatTag == 1 && (s->mBits == 8 || s->mBits == 16))
    {
        s->mTotalFrames = s->mDataSize / (s->mChannels * s->mBits / 8);
        return true;
    }
    if (s->mFormatTag == 0x11 && s->mBits == 4 && s->mBlockAlign > 4 * s->mChannels)
    {
        if (!s->mFramesPerBlock)
        {
            s->mFramesPerBlock = (s->mBlockAlign - 4 * s->mChannels) * 8 / (4 * s->mChannels) + 1;
        }
        s->mTotalFrames = s->mDataSize / s->mBlockAlign * s->mFramesPerBlock;
        return true;
    }
    return false;
}

// Decodes the block holding frame f into mBuf.
bool FillStream(Stream* s, u32 f)
{
    if (f >= s->mTotalFrames)
    {
        return false;
    }
    const int ch = s->mChannels;
    if (s->mFormatTag == 1)
    {
        const u32 chunkFrames = 4096;
        const u32 first = f / chunkFrames * chunkFrames;
        const u32 count = SDL_min(chunkFrames, s->mTotalFrames - first);
        const int bytes = s->mBits / 8;
        std::vector<u8> raw(count * ch * bytes);
        fseek(s->mFile, s->mDataStart + (long)(first * ch * bytes), SEEK_SET);
        const size_t got = fread(raw.data(), 1, raw.size(), s->mFile) / (ch * bytes);
        s->mBuf.resize(got * ch);
        for (size_t i = 0; i < got * ch; i++)
        {
            s->mBuf[i] = bytes == 2 ? (s16)Rd16(&raw[i * 2]) : (s16)((raw[i] - 128) << 8);
        }
        s->mBufFirst = first;
        return got > 0;
    }
    const u32 block = f / s->mFramesPerBlock;
    std::vector<u8> raw(s->mBlockAlign);
    fseek(s->mFile, s->mDataStart + (long)block * s->mBlockAlign, SEEK_SET);
    if (fread(raw.data(), 1, raw.size(), s->mFile) != raw.size())
    {
        return false;
    }
    s->mBuf.assign((size_t)s->mFramesPerBlock * ch, 0);
    int pred[2];
    int index[2];
    for (int c = 0; c < ch; c++)
    {
        pred[c] = (s16)Rd16(&raw[c * 4]);
        index[c] = SDL_min(raw[c * 4 + 2], 88);
        s->mBuf[c] = (s16)pred[c];
    }
    // After the headers, 4 bytes (8 nibbles) per channel in turn.
    const u8* p = &raw[4 * ch];
    const u8* end = raw.data() + raw.size();
    int frame = 1;
    while (p + 4 * ch <= end && frame < s->mFramesPerBlock)
    {
        for (int c = 0; c < ch; c++)
        {
            for (int k = 0; k < 8 && frame + k < s->mFramesPerBlock; k++)
            {
                const int nibble = (p[k >> 1] >> ((k & 1) * 4)) & 15;
                s->mBuf[(frame + k) * ch + c] = ImaNibble(nibble, pred[c], index[c]);
            }
            p += 4;
        }
        frame += 8;
    }
    s->mBufFirst = block * s->mFramesPerBlock;
    return true;
}

bool StreamFrame(Stream* s, u32 f, float* pL, float* pR)
{
    if (f < s->mBufFirst || f >= s->mBufFirst + s->mBuf.size() / s->mChannels)
    {
        if (!FillStream(s, f))
        {
            return false;
        }
    }
    const s16* p = &s->mBuf[(f - s->mBufFirst) * s->mChannels];
    *pL = p[0] / 32768.0f;
    *pR = p[s->mChannels - 1] / 32768.0f;
    return true;
}

float SampleValue(const Sample* s, u32 frame, int channel)
{
    const bool stereo = (s->mFormat & DIG_F_STEREO_MASK) != 0;
    const bool b16 = (s->mFormat & DIG_F_16BITS_MASK) != 0;
    const u32 idx = frame * (stereo ? 2 : 1) + (stereo ? channel : 0);
    if (b16)
    {
        return (s16)Rd16(s->mData + idx * 2) / 32768.0f;
    }
    const u8 v = s->mData[idx];
    return s->mSigned8 ? (s8)v / 128.0f : (v - 128) / 128.0f;
}

u32 SampleFrames(const Sample* s)
{
    const int frameBytes = ((s->mFormat & DIG_F_STEREO_MASK) ? 2 : 1) * ((s->mFormat & DIG_F_16BITS_MASK) ? 2 : 1);
    return s->mLen / frameBytes;
}

void MixSample(Sample* s, float* out, int frames)
{
    const u32 total = SampleFrames(s);
    if (!s->mData || !total)
    {
        s->mStatus = SMP_DONE;
        return;
    }
    const int frameBytes = ((s->mFormat & DIG_F_STEREO_MASK) ? 2 : 1) * ((s->mFormat & DIG_F_16BITS_MASK) ? 2 : 1);
    const u32 loopStart = SDL_min((u32)SDL_max(s->mLoopStart, 0) / frameBytes, total);
    const u32 loopEnd = s->mLoopEnd < 0 ? total : SDL_min((u32)s->mLoopEnd / frameBytes, total);
    const double step = (double)s->mRate / kOutRate;
    const float vol = s->mVolume / 127.0f * sDriver->mMasterVolume / 127.0f;
    const float left = vol * SDL_min(1.0f, (127 - s->mPan) / 63.5f);
    const float right = vol * SDL_min(1.0f, s->mPan / 63.5f);
    for (int i = 0; i < frames; i++)
    {
        u32 f = (u32)s->mPos;
        if (f >= loopEnd)
        {
            if (s->mLoopCount == 1)
            {
                s->mStatus = SMP_DONE;
                return;
            }
            if (s->mLoopCount > 1)
            {
                s->mLoopCount--;
            }
            s->mPos = loopStart + (s->mPos - loopEnd);
            f = (u32)s->mPos;
            if (f >= loopEnd)
            {
                s->mStatus = SMP_DONE;
                return;
            }
        }
        const float t = (float)(s->mPos - f);
        const u32 f2 = f + 1 < loopEnd ? f + 1 : f;
        const float l = SampleValue(s, f, 0) * (1 - t) + SampleValue(s, f2, 0) * t;
        const float r = SampleValue(s, f, 1) * (1 - t) + SampleValue(s, f2, 1) * t;
        out[i * 2] += l * left;
        out[i * 2 + 1] += r * right;
        s->mPos += step;
    }
}

void MixStream(Stream* s, float* out, int frames)
{
    const double step = (double)(s->mPlayRate ? s->mPlayRate : s->mRate) / kOutRate;
    const float vol = s->mVolume / 127.0f * sDriver->mMasterVolume / 127.0f;
    for (int i = 0; i < frames; i++)
    {
        u32 f = (u32)s->mPos;
        if (f >= s->mTotalFrames)
        {
            if (s->mLoopCount == 1)
            {
                s->mStatus = SMP_DONE;
                return;
            }
            if (s->mLoopCount > 1)
            {
                s->mLoopCount--;
            }
            s->mPos = 0;
            f = 0;
        }
        float l;
        float r;
        if (!StreamFrame(s, f, &l, &r))
        {
            s->mStatus = SMP_DONE;
            return;
        }
        out[i * 2] += l * vol;
        out[i * 2 + 1] += r * vol;
        s->mPos += step;
    }
}

void SDLCALL AudioCallback(void*, SDL_AudioStream* stream, int additional, int)
{
    const int frames = additional / (int)(2 * sizeof(float));
    if (frames <= 0)
    {
        return;
    }
    std::vector<float> mix((size_t)frames * 2, 0.0f);
    {
        std::lock_guard<std::recursive_mutex> lock(Lock());
        if (sDriver)
        {
            for (Sample* s : sDriver->mSamples)
            {
                if (s->mStatus == SMP_PLAYING)
                {
                    MixSample(s, mix.data(), frames);
                }
            }
            for (Stream* s : sDriver->mStreams)
            {
                if (s->mStatus == SMP_PLAYING)
                {
                    MixStream(s, mix.data(), frames);
                }
            }
        }
    }
    for (float& v : mix)
    {
        v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    }
    SDL_PutAudioStreamData(stream, mix.data(), (int)(mix.size() * sizeof(float)));
}

Sample* S(HSAMPLE h)
{
    return reinterpret_cast<Sample*>(h);
}

Stream* St(HSTREAM h)
{
    return reinterpret_cast<Stream*>(h);
}
} // namespace

#define LOCK std::lock_guard<std::recursive_mutex> lock(Lock())

DXDEC S32 AILCALL AIL_enumerate_3D_providers(HPROENUM FAR*, HPROVIDER FAR*, C8 FAR* FAR*)
{
    return 0;
}

DXDEC void FAR* AILCALL AIL_mem_alloc_lock(U32 size)
{
    return calloc(1, size);
}

DXDEC void AILCALL AIL_mem_free_lock(void FAR* ptr)
{
    free(ptr);
}

DXDEC S32 AILCALL AIL_startup(void)
{
    return SDL_InitSubSystem(SDL_INIT_AUDIO) ? 1 : 0;
}

DXDEC S32 AILCALL AIL_get_preference(U32)
{
    return 0;
}

DXDEC void AILCALL AIL_shutdown(void)
{
}

DXDEC S32 AILCALL AIL_set_preference(U32, S32)
{
    return 0;
}

// Intervals of 1/60 s.
DXDEC void AILCALL AIL_delay(S32 intervals)
{
    SDL_Delay((Uint32)(intervals * 1000 / 60));
}

DXDEC S32 AILCALL AIL_waveOutOpen(HDIGDRIVER FAR* drvr, LPHWAVEOUT FAR*, S32, LPWAVEFORMAT)
{
    LOCK;
    if (getenv("GTA2_NO_SOUND"))
    {
        return -1;
    }
    const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, kOutRate};
    SDL_AudioStream* pDevice = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioCallback, NULL);
    if (!pDevice)
    {
        SDL_Log("Audio: %s", SDL_GetError());
        return -1;
    }
    sDriver = new Driver();
    sDriver->mDevice = pDevice;
    SDL_ResumeAudioStreamDevice(pDevice);
    *drvr = reinterpret_cast<HDIGDRIVER>(sDriver);
    return 0;
}

DXDEC void AILCALL AIL_waveOutClose(HDIGDRIVER)
{
    SDL_AudioStream* pDevice = NULL;
    {
        LOCK;
        if (!sDriver)
        {
            return;
        }
        pDevice = sDriver->mDevice;
    }
    SDL_DestroyAudioStream(pDevice);
    LOCK;
    delete sDriver;
    sDriver = NULL;
}

DXDEC S32 AILCALL AIL_digital_handle_release(HDIGDRIVER)
{
    if (sDriver)
    {
        SDL_PauseAudioStreamDevice(sDriver->mDevice);
    }
    return 1;
}

DXDEC S32 AILCALL AIL_digital_handle_reacquire(HDIGDRIVER)
{
    if (sDriver)
    {
        SDL_ResumeAudioStreamDevice(sDriver->mDevice);
    }
    return 1;
}

DXDEC HSAMPLE AILCALL AIL_allocate_sample_handle(HDIGDRIVER)
{
    LOCK;
    if (!sDriver)
    {
        return NULL;
    }
    Sample* s = new Sample();
    sDriver->mSamples.push_back(s);
    return reinterpret_cast<HSAMPLE>(s);
}

DXDEC void AILCALL AIL_release_sample_handle(HSAMPLE h)
{
    LOCK;
    if (sDriver && h)
    {
        sDriver->mSamples.erase(std::find(sDriver->mSamples.begin(), sDriver->mSamples.end(), S(h)));
        delete S(h);
    }
}

DXDEC void AILCALL AIL_init_sample(HSAMPLE h)
{
    LOCK;
    *S(h) = Sample();
}

DXDEC void AILCALL AIL_set_sample_address(HSAMPLE h, void FAR* start, U32 len)
{
    LOCK;
    S(h)->mData = static_cast<const u8*>(start);
    S(h)->mLen = len;
    S(h)->mPos = 0;
    S(h)->mStatus = SMP_DONE;
}

DXDEC void AILCALL AIL_set_sample_type(HSAMPLE h, S32 format, U32 flags)
{
    LOCK;
    S(h)->mFormat = format;
    S(h)->mSigned8 = (flags & DIG_PCM_SIGN) != 0;
}

DXDEC void AILCALL AIL_start_sample(HSAMPLE h)
{
    LOCK;
    S(h)->mPos = 0;
    S(h)->mStatus = SMP_PLAYING;
}

DXDEC void AILCALL AIL_end_sample(HSAMPLE h)
{
    LOCK;
    S(h)->mStatus = SMP_DONE;
}

DXDEC void AILCALL AIL_set_sample_playback_rate(HSAMPLE h, S32 playback_rate)
{
    LOCK;
    S(h)->mRate = playback_rate > 0 ? playback_rate : 1;
}

DXDEC void AILCALL AIL_set_sample_volume(HSAMPLE h, S32 volume)
{
    LOCK;
    S(h)->mVolume = SDL_clamp(volume, 0, 127);
}

DXDEC void AILCALL AIL_set_sample_pan(HSAMPLE h, S32 pan)
{
    LOCK;
    S(h)->mPan = SDL_clamp(pan, 0, 127);
}

DXDEC void AILCALL AIL_set_sample_loop_count(HSAMPLE h, S32 loop_count)
{
    LOCK;
    S(h)->mLoopCount = loop_count;
}

DXDEC void AILCALL AIL_set_sample_loop_block(HSAMPLE h, S32 loop_start_offset, S32 loop_end_offset)
{
    LOCK;
    S(h)->mLoopStart = loop_start_offset;
    S(h)->mLoopEnd = loop_end_offset;
}

DXDEC U32 AILCALL AIL_sample_status(HSAMPLE h)
{
    LOCK;
    return h ? S(h)->mStatus : SMP_DONE;
}

DXDEC void AILCALL AIL_set_digital_master_volume(HDIGDRIVER, S32 master_volume)
{
    LOCK;
    if (sDriver)
    {
        sDriver->mMasterVolume = SDL_clamp(master_volume, 0, 127);
    }
}

DXDEC HSTREAM AILCALL AIL_open_stream(HDIGDRIVER, char FAR* filename, S32)
{
    Stream* s = new Stream();
    if (!sDriver || !OpenWav(s, filename))
    {
        if (s->mFile)
        {
            fclose(s->mFile);
        }
        delete s;
        return NULL;
    }
    LOCK;
    sDriver->mStreams.push_back(s);
    return reinterpret_cast<HSTREAM>(s);
}

DXDEC void AILCALL AIL_close_stream(HSTREAM h)
{
    LOCK;
    if (sDriver && h)
    {
        sDriver->mStreams.erase(std::find(sDriver->mStreams.begin(), sDriver->mStreams.end(), St(h)));
        fclose(St(h)->mFile);
        delete St(h);
    }
}

DXDEC void AILCALL AIL_start_stream(HSTREAM h)
{
    LOCK;
    St(h)->mPos = 0;
    St(h)->mStatus = SMP_PLAYING;
}

DXDEC void AILCALL AIL_set_stream_volume(HSTREAM h, S32 volume)
{
    LOCK;
    St(h)->mVolume = SDL_clamp(volume, 0, 127);
}

DXDEC S32 AILCALL AIL_stream_volume(HSTREAM h)
{
    LOCK;
    return St(h)->mVolume;
}

DXDEC void AILCALL AIL_set_stream_playback_rate(HSTREAM h, S32 rate)
{
    LOCK;
    St(h)->mPlayRate = rate;
}

DXDEC S32 AILCALL AIL_stream_playback_rate(HSTREAM h)
{
    LOCK;
    return St(h)->mPlayRate ? St(h)->mPlayRate : St(h)->mRate;
}

DXDEC void AILCALL AIL_set_stream_loop_count(HSTREAM h, S32 count)
{
    LOCK;
    St(h)->mLoopCount = count;
}

DXDEC S32 AILCALL AIL_stream_status(HSTREAM h)
{
    LOCK;
    return h ? (S32)St(h)->mStatus : SMP_DONE;
}

DXDEC void AILCALL AIL_set_stream_ms_position(HSTREAM h, S32 milliseconds)
{
    LOCK;
    Stream* s = St(h);
    s->mPos = SDL_min((double)milliseconds * s->mRate / 1000.0, (double)s->mTotalFrames);
}

DXDEC void AILCALL AIL_stream_ms_position(HSTREAM h, S32 FAR* total_milliseconds, S32 FAR* current_milliseconds)
{
    LOCK;
    Stream* s = St(h);
    if (total_milliseconds)
    {
        *total_milliseconds = (S32)((long long)s->mTotalFrames * 1000 / s->mRate);
    }
    if (current_milliseconds)
    {
        *current_milliseconds = (S32)(s->mPos * 1000 / s->mRate);
    }
}
DXDEC M3DRESULT AILCALL AIL_open_3D_provider(HPROVIDER lib)
{
    return 0;
}

DXDEC void AILCALL AIL_close_3D_provider(HPROVIDER lib)
{
}

DXDEC void AILCALL AIL_3D_provider_attribute(HPROVIDER lib, C8 FAR* name, void FAR* val)
{
}

DXDEC void AILCALL AIL_set_3D_provider_preference(HPROVIDER lib, C8 FAR* name, void FAR* val)
{
}

DXDEC H3DSAMPLE AILCALL AIL_allocate_3D_sample_handle(HPROVIDER lib)
{
    return 0;
}

DXDEC void AILCALL AIL_release_3D_sample_handle(H3DSAMPLE S)
{
}

DXDEC void AILCALL AIL_start_3D_sample(H3DSAMPLE S)
{
}

DXDEC void AILCALL AIL_end_3D_sample(H3DSAMPLE S)
{
}

DXDEC S32 AILCALL AIL_set_3D_sample_info(H3DSAMPLE S, AILSOUNDINFO FAR* info)
{
    return 0;
}

DXDEC void AILCALL AIL_set_3D_sample_volume(H3DSAMPLE S, S32 volume)
{
}

DXDEC void AILCALL AIL_set_3D_sample_playback_rate(H3DSAMPLE S, S32 playback_rate)
{
}

DXDEC void AILCALL AIL_set_3D_sample_loop_count(H3DSAMPLE S, U32 loops)
{
}

DXDEC void AILCALL AIL_set_3D_sample_loop_block(H3DSAMPLE S, S32 loop_start_offset, S32 loop_end_offset)
{
}

DXDEC U32 AILCALL AIL_3D_sample_status(H3DSAMPLE S)
{
    return 0;
}

DXDEC void AILCALL
AIL_3D_sample_float_distances(H3DSAMPLE S, F32 FAR* max_front_dist, F32 FAR* min_front_dist, F32 FAR* max_back_dist, F32 FAR* min_back_dist)
{
}

DXDEC void AILCALL AIL_set_3D_position(H3DPOBJECT obj, F32 X, F32 Y, F32 Z)
{
}

DXDEC void AILCALL
AIL_set_3D_sample_float_distances(H3DSAMPLE S, F32 max_front_dist, F32 min_front_dist, F32 max_back_dist, F32 min_back_dist)
{
}