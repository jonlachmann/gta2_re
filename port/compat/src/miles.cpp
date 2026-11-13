// Miles Sound System (the AIL_* subset GTA2 uses) on SDL3 audio: 2D samples from the game's sound
// banks, and WAV streams (radio stations, voices) decoded while they play, PCM or IMA ADPCM.
// The 3D provider is in miles3d.cpp.
#include "types.hpp"
#include "3rdParty/mss.h"
#include "miles3d.hpp"
#include "port_platform.hpp"
#include "../../src/net_hooks.hpp" // gPortNoAudio
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <algorithm>
#include <mutex>
#include <vector>

#undef fopen

namespace
{
const int kOutRate = Miles3D_kRate;

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
    float mGain[2] = {-1, -1}; // left and right as mixed, -1 until the first frame
    int mFade = -1;            // frames left in a fade-out, -1 when not stopping
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
    float mGain = -1;
    int mFade = -1;

    // The frames on each side of the position, for the interpolation.
    u32 mCurFrame = UINT_MAX;
    float mCur[2] = {};
    float mNext[2] = {};
};

struct Driver
{
    SDL_AudioStream* mDevice = NULL;
    S32 mMasterVolume = 127;
    std::vector<Sample*> mSamples;
    std::vector<Stream*> mStreams;
    // Copies of samples stopped while playing, and closed streams, until their fade-out ends.
    std::vector<Sample*> mFadingSamples;
    std::vector<Stream*> mFadingStreams;
};

// A stop fades out, and a volume or pan change ramps, over this many frames (7.5 ms), as a step in the
// waveform clicks.
const int kRampFrames = kOutRate * 3 / 400;

float Ramp(float current, float target)
{
    const float step = 1.0f / kRampFrames;
    return current < 0 ? target : current + SDL_clamp(target - current, -step, step);
}

// 1 while playing, then down to 0 over a fade-out; false once it ended.
bool FadeStep(int* pFade, float* pGain)
{
    if (*pFade == 0)
    {
        return false;
    }
    *pGain = *pFade > 0 ? (float)(*pFade)-- / kRampFrames : 1.0f;
    return true;
}

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

bool StreamFrame(Stream* s, u32 f, float* pL, float* pR);

// Frames f and f + 1; moving on by one frame reads only the new one, so a block is not read again.
bool StreamFrames(Stream* s, u32 f)
{
    if (f == s->mCurFrame)
    {
        return true;
    }
    if (s->mCurFrame != UINT_MAX && f == s->mCurFrame + 1)
    {
        s->mCur[0] = s->mNext[0];
        s->mCur[1] = s->mNext[1];
    }
    else if (!StreamFrame(s, f, &s->mCur[0], &s->mCur[1]))
    {
        return false;
    }
    s->mCurFrame = f;
    const u32 next = f + 1 < s->mTotalFrames ? f + 1 : (s->mLoopCount == 1 ? f : 0);
    if (!StreamFrame(s, next, &s->mNext[0], &s->mNext[1]))
    {
        s->mNext[0] = s->mCur[0];
        s->mNext[1] = s->mCur[1];
    }
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

// The value at a data index: 16-bit signed, or 8-bit with flip8 0x80 for unsigned data.
template <bool k16>
float SampleValue(const u8* pData, u32 idx, u8 flip8)
{
    if (k16)
    {
        Uint16 v;
        SDL_memcpy(&v, pData + idx * 2, 2);
        return (s16)SDL_Swap16LE(v) / 32768.0f;
    }
    return (s8)(pData[idx] ^ flip8) / 128.0f;
}

u32 SampleFrames(const Sample* s)
{
    const int frameBytes = ((s->mFormat & DIG_F_STEREO_MASK) ? 2 : 1) * ((s->mFormat & DIG_F_16BITS_MASK) ? 2 : 1);
    return s->mLen / frameBytes;
}

// The mixing loop for one data format, on local copies of the sample's state. kSteady: neither fading out
// nor ramping, the gains already at their targets.
template <bool kStereo, bool k16, bool kSteady>
void MixSampleAs(Sample* s, float* out, int frames, u32 loopStart, u32 loopEnd, float left, float right)
{
    const double step = (double)s->mRate / kOutRate;
    const u8* pData = s->mData;
    const u8 flip8 = s->mSigned8 ? 0 : 0x80;
    double pos = s->mPos;
    float gainL = s->mGain[0];
    float gainR = s->mGain[1];
    int fadeLeft = s->mFade;
    for (int i = 0; i < frames; i++)
    {
        float fade = 1.0f;
        if (!kSteady)
        {
            if (!FadeStep(&fadeLeft, &fade))
            {
                s->mStatus = SMP_DONE;
                break;
            }
            gainL = Ramp(gainL, left);
            gainR = Ramp(gainR, right);
        }
        u32 f = (u32)pos;
        if (f >= loopEnd)
        {
            if (s->mLoopCount == 1)
            {
                s->mStatus = SMP_DONE;
                break;
            }
            if (s->mLoopCount > 1)
            {
                s->mLoopCount--;
            }
            pos = loopStart + (pos - loopEnd);
            f = (u32)pos;
            if (f >= loopEnd)
            {
                s->mStatus = SMP_DONE;
                break;
            }
        }
        const float t = (float)(pos - f);
        const u32 f2 = f + 1 < loopEnd ? f + 1 : f;
        float l;
        float r;
        if (kStereo)
        {
            l = SampleValue<k16>(pData, f * 2, flip8) * (1 - t) + SampleValue<k16>(pData, f2 * 2, flip8) * t;
            r = SampleValue<k16>(pData, f * 2 + 1, flip8) * (1 - t) + SampleValue<k16>(pData, f2 * 2 + 1, flip8) * t;
        }
        else
        {
            l = r = SampleValue<k16>(pData, f, flip8) * (1 - t) + SampleValue<k16>(pData, f2, flip8) * t;
        }
        out[i * 2] += l * gainL * fade;
        out[i * 2 + 1] += r * gainR * fade;
        pos += step;
    }
    s->mPos = pos;
    s->mGain[0] = gainL;
    s->mGain[1] = gainR;
    s->mFade = fadeLeft;
}

void MixSample(Sample* s, float* out, int frames)
{
    const u32 total = SampleFrames(s);
    if (!s->mData || !total)
    {
        s->mStatus = SMP_DONE;
        return;
    }
    const bool stereo = (s->mFormat & DIG_F_STEREO_MASK) != 0;
    const bool b16 = (s->mFormat & DIG_F_16BITS_MASK) != 0;
    const int frameBytes = (stereo ? 2 : 1) * (b16 ? 2 : 1);
    const u32 loopStart = SDL_min((u32)SDL_max(s->mLoopStart, 0) / frameBytes, total);
    const u32 loopEnd = s->mLoopEnd < 0 ? total : SDL_min((u32)s->mLoopEnd / frameBytes, total);
    const float vol = s->mVolume / 127.0f * sDriver->mMasterVolume / 127.0f;
    const float left = vol * SDL_min(1.0f, (127 - s->mPan) / 63.5f);
    const float right = vol * SDL_min(1.0f, s->mPan / 63.5f);
    void (*mix)(Sample*, float*, int, u32, u32, float, float);
    if (s->mFade < 0 && s->mGain[0] == left && s->mGain[1] == right)
    {
        mix = stereo ? (b16 ? MixSampleAs<true, true, true> : MixSampleAs<true, false, true>)
                     : (b16 ? MixSampleAs<false, true, true> : MixSampleAs<false, false, true>);
    }
    else
    {
        mix = stereo ? (b16 ? MixSampleAs<true, true, false> : MixSampleAs<true, false, false>)
                     : (b16 ? MixSampleAs<false, true, false> : MixSampleAs<false, false, false>);
    }
    mix(s, out, frames, loopStart, loopEnd, left, right);
}

void MixStream(Stream* s, float* out, int frames)
{
    const double step = (double)(s->mPlayRate ? s->mPlayRate : s->mRate) / kOutRate;
    const float vol = s->mVolume / 127.0f * sDriver->mMasterVolume / 127.0f;
    for (int i = 0; i < frames; i++)
    {
        float fade;
        if (!FadeStep(&s->mFade, &fade))
        {
            s->mStatus = SMP_DONE;
            return;
        }
        s->mGain = Ramp(s->mGain, vol);
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
        if (!StreamFrames(s, f))
        {
            s->mStatus = SMP_DONE;
            return;
        }
        const float t = (float)(s->mPos - f);
        const float gain = s->mGain * fade;
        out[i * 2] += (s->mCur[0] * (1 - t) + s->mNext[0] * t) * gain;
        out[i * 2 + 1] += (s->mCur[1] * (1 - t) + s->mNext[1] * t) * gain;
        s->mPos += step;
    }
}

// Sounds at full scale (the frontend's are) sum past it when they overlap. Rather than clipping, the
// mix is turned down for as long as needed, starting kLookahead frames ahead of the peak.
class Limiter
{
  public:
    void Process(float* pMix, int frames)
    {
        const float kCeiling = 0.99f;
        const float release = 1.0f - expf(-1.0f / (0.15f * kOutRate));
        for (int i = 0; i < frames; i++)
        {
            float* p = &pMix[i * 2];
            const float peak = SDL_max(fabsf(p[0]), fabsf(p[1]));
            const int in = mPos;
            mDelay[in * 2] = p[0];
            mDelay[in * 2 + 1] = p[1];
            mLoud -= mNeeded[in] < 1.0f;
            mNeeded[in] = peak > kCeiling ? kCeiling / peak : 1.0f;
            mLoud += mNeeded[in] < 1.0f;
            mPos = (mPos + 1) % kLookahead;
            // A linear ramp down to each gain needed in the lookahead, the oldest frame needing its own.
            float gain = mGain + (1.0f - mGain) * release;
            for (int k = 0; mLoud && k < kLookahead; k++)
            {
                const float needed = mNeeded[(mPos + k) % kLookahead];
                gain = SDL_min(gain, needed + (1.0f - needed) * k / kLookahead);
            }
            mGain = gain;
            p[0] = mDelay[mPos * 2] * gain;
            p[1] = mDelay[mPos * 2 + 1] * gain;
        }
    }

  private:
    static const int kLookahead = 64;
    float mDelay[kLookahead * 2] = {};
    float mNeeded[kLookahead] = {};
    int mPos = 0;
    int mLoud = kLookahead; // entries of mNeeded below 1: without any, the gain only rises
    float mGain = 1.0f;
};

Limiter sLimiter;

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
            for (size_t i = 0; i < sDriver->mFadingSamples.size();)
            {
                Sample* s = sDriver->mFadingSamples[i];
                MixSample(s, mix.data(), frames);
                if (s->mStatus == SMP_PLAYING)
                {
                    i++;
                    continue;
                }
                delete s;
                sDriver->mFadingSamples.erase(sDriver->mFadingSamples.begin() + i);
            }
            for (size_t i = 0; i < sDriver->mFadingStreams.size();)
            {
                Stream* s = sDriver->mFadingStreams[i];
                MixStream(s, mix.data(), frames);
                if (s->mStatus == SMP_PLAYING)
                {
                    i++;
                    continue;
                }
                fclose(s->mFile);
                delete s;
                sDriver->mFadingStreams.erase(sDriver->mFadingStreams.begin() + i);
            }
        }
    }
    Miles3D_Render(mix.data(), frames);
    sLimiter.Process(mix.data(), frames);
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

// Stops the sample at once for the game, while a copy of it fades out.
void StopSample(Sample* s)
{
    if (sDriver && s->mStatus == SMP_PLAYING && s->mFade < 0)
    {
        Sample* pCopy = new Sample(*s);
        pCopy->mFade = kRampFrames;
        sDriver->mFadingSamples.push_back(pCopy);
    }
    s->mStatus = SMP_DONE;
}

void DropFading()
{
    if (!sDriver)
    {
        return;
    }
    for (Sample* s : sDriver->mFadingSamples)
    {
        delete s;
    }
    for (Stream* s : sDriver->mFadingStreams)
    {
        fclose(s->mFile);
        delete s;
    }
    sDriver->mFadingSamples.clear();
    sDriver->mFadingStreams.clear();
}

Stream* St(HSTREAM h)
{
    return reinterpret_cast<Stream*>(h);
}
} // namespace

#define LOCK std::lock_guard<std::recursive_mutex> lock(Lock())

DXDEC void FAR* AILCALL AIL_mem_alloc_lock(U32 size)
{
    return calloc(1, size);
}

DXDEC void AILCALL AIL_mem_free_lock(void FAR* ptr)
{
    // The fading copies of samples may point into it.
    LOCK;
    DropFading();
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

// The game closes and reopens the driver to switch 3D sound on or off: the SDL device stays open
// (silent while no driver), since reopening it takes a noticeable time.
static SDL_AudioStream* sDevice;

DXDEC S32 AILCALL AIL_waveOutOpen(HDIGDRIVER FAR* drvr, LPHWAVEOUT FAR*, S32, LPWAVEFORMAT)
{
    LOCK;
    if (getenv("GTA2_NO_SOUND") || gPortNoAudio) // the game goes on without sound (a network server's: nobody hears it)
    {
        return -1;
    }
    if (!sDevice)
    {
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, kOutRate};
        sDevice = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioCallback, NULL);
        if (!sDevice)
        {
            SDL_Log("Audio: %s", SDL_GetError());
            return -1;
        }
        SDL_Log("Audio: %s", SDL_GetCurrentAudioDriver());
    }
    sDriver = new Driver();
    sDriver->mDevice = sDevice;
    SDL_ResumeAudioStreamDevice(sDevice);
    *drvr = reinterpret_cast<HDIGDRIVER>(sDriver);
    return 0;
}

DXDEC void AILCALL AIL_waveOutClose(HDIGDRIVER)
{
    LOCK;
    DropFading();
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
        StopSample(S(h));
        sDriver->mSamples.erase(std::find(sDriver->mSamples.begin(), sDriver->mSamples.end(), S(h)));
        delete S(h);
    }
}

DXDEC void AILCALL AIL_init_sample(HSAMPLE h)
{
    LOCK;
    StopSample(S(h));
    *S(h) = Sample();
}

DXDEC void AILCALL AIL_set_sample_address(HSAMPLE h, void FAR* start, U32 len)
{
    LOCK;
    StopSample(S(h));
    S(h)->mData = static_cast<const u8*>(start);
    S(h)->mLen = len;
    S(h)->mPos = 0;
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
    StopSample(S(h));
    S(h)->mPos = 0;
    S(h)->mGain[0] = S(h)->mGain[1] = -1;
    S(h)->mStatus = SMP_PLAYING;
}

DXDEC void AILCALL AIL_end_sample(HSAMPLE h)
{
    LOCK;
    StopSample(S(h));
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
    Miles3D_SetMasterVolume(SDL_clamp(master_volume, 0, 127));
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
        if (St(h)->mStatus == SMP_PLAYING)
        {
            St(h)->mFade = kRampFrames;
            sDriver->mFadingStreams.push_back(St(h));
            return;
        }
        fclose(St(h)->mFile);
        delete St(h);
    }
}

DXDEC void AILCALL AIL_start_stream(HSTREAM h)
{
    LOCK;
    St(h)->mPos = 0;
    St(h)->mGain = -1;
    St(h)->mCurFrame = UINT_MAX;
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
    s->mCurFrame = UINT_MAX;
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
