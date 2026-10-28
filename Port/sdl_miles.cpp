// Port (docs/port.md): the part of the Miles Sound System API that cSampleManager.cpp uses, on SDL3 audio.
// A software mixer: 2D samples from the sound bank in memory, and streams (WAV files, PCM or IMA ADPCM,
// decoded whole with SDL_LoadWAV). Handles point at our own voices; the game never looks inside them.
// ponytail: no 3D provider is offered, so the game stays on its 2D (stereo pan) path; streams are decoded
// whole on open (a few MB each). Add positional audio or streaming decode if either matters.
#include "Source/3rdParty/mss.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace
{
const int kOutRate = 44100;

struct Voice
{
    bool used;
    bool playing;
    bool isStream;
    const Uint8* data; // sample: the game's memory; stream: pcm below
    U32 len; // bytes
    S32 format; // DIG_F_*
    bool sign; // 8-bit: signed data (DIG_PCM_SIGN)
    S32 rate;
    S32 volume; // 0..127
    S32 pan; // 0 left, 64 centre, 127 right
    S32 loops; // 0 = forever
    U32 loopStart, loopEnd; // bytes; loopEnd 0 = end of data
    double pos; // in frames
    std::vector<Uint8> pcm; // streams: decoded 16-bit PCM
};

SDL_Mutex* gLock;
SDL_AudioStream* gOut;
S32 gMasterVolume = 127;
std::vector<Voice*> gVoices;

int Channels(const Voice& v)
{
    return (v.format & DIG_F_STEREO_MASK) ? 2 : 1;
}
int BytesPerFrame(const Voice& v)
{
    return Channels(v) * ((v.format & DIG_F_16BITS_MASK) ? 2 : 1);
}

float SampleAt(const Voice& v, U32 frame, int channel)
{
    const Uint8* p = v.data + frame * BytesPerFrame(v);
    if (v.format & DIG_F_16BITS_MASK)
    {
        Sint16 s;
        memcpy(&s, p + channel * 2, 2);
        return s / 32768.0f;
    }
    const Uint8 b = p[channel];
    return v.sign ? (Sint8)b / 128.0f : (b - 128) / 128.0f;
}

// Adds one voice into the stereo float buffer; returns false when it has finished.
bool MixVoice(Voice& v, float* out, int frames)
{
    const int bpf = BytesPerFrame(v);
    const U32 total = v.len / bpf;
    if (!v.data || !total || v.rate <= 0)
    {
        return false;
    }
    const U32 loopStart = v.loopStart / bpf;
    const U32 loopEnd = (v.loopEnd && v.loopEnd / bpf <= total) ? v.loopEnd / bpf : total;
    const double step = (double)v.rate / kOutRate;
    const float gain = (v.volume / 127.0f) * (gMasterVolume / 127.0f);
    // Centre (64) plays at full volume on both sides; panning attenuates the other side linearly.
    const float pan = v.pan / 127.0f;
    const float gl = gain * (pan <= 0.5f ? 1.0f : 2.0f * (1.0f - pan));
    const float gr = gain * (pan >= 0.5f ? 1.0f : 2.0f * pan);
    const bool stereo = Channels(v) == 2;
    for (int i = 0; i < frames; i++)
    {
        U32 f = (U32)v.pos;
        if (f >= loopEnd)
        {
            if (v.loops == 1)
            {
                return false;
            }
            if (v.loops > 1)
            {
                v.loops--;
            }
            v.pos = loopStart + (v.pos - loopEnd);
            f = (U32)v.pos;
            if (f >= loopEnd)
            {
                return false;
            }
        }
        const float l = SampleAt(v, f, 0);
        const float r = stereo ? SampleAt(v, f, 1) : l;
        out[i * 2] += l * gl;
        out[i * 2 + 1] += r * gr;
        v.pos += step;
    }
    return true;
}

void SDLCALL Mix(void*, SDL_AudioStream* stream, int additional, int);
} // namespace

// Mixes all playing voices into out (stereo float, frames * 2 values, 44.1 kHz). Also used by
// Port/tests/sdl_miles_test.cpp.
void Port_MilesMix(float* out, int frames)
{
    memset(out, 0, frames * 2 * sizeof(float));
    SDL_LockMutex(gLock);
    for (size_t i = 0; i < gVoices.size(); i++)
    {
        Voice& v = *gVoices[i];
        if (v.used && v.playing && !MixVoice(v, out, frames))
        {
            v.playing = false;
        }
    }
    SDL_UnlockMutex(gLock);
    for (int i = 0; i < frames * 2; i++)
    {
        out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
    }
}

namespace
{
void SDLCALL Mix(void*, SDL_AudioStream* stream, int additional, int)
{
    const int frames = additional / (int)(2 * sizeof(float));
    if (frames > 0)
    {
        std::vector<float> buf(frames * 2);
        Port_MilesMix(&buf[0], frames);
        SDL_PutAudioStreamData(stream, &buf[0], frames * 2 * sizeof(float));
    }
}

Voice* NewVoice(bool isStream)
{
    Voice* v = new Voice();
    v->used = true;
    v->isStream = isStream;
    v->volume = 127;
    v->pan = 64;
    v->loops = 1;
    v->rate = 22050;
    SDL_LockMutex(gLock);
    gVoices.push_back(v);
    SDL_UnlockMutex(gLock);
    return v;
}

void FreeVoice(Voice* v)
{
    SDL_LockMutex(gLock);
    for (size_t i = 0; i < gVoices.size(); i++)
    {
        if (gVoices[i] == v)
        {
            gVoices.erase(gVoices.begin() + i);
            break;
        }
    }
    SDL_UnlockMutex(gLock);
    delete v;
}

// Every setter runs under the mixer's lock.
struct Locked
{
    Locked()
    {
        SDL_LockMutex(gLock);
    }
    ~Locked()
    {
        SDL_UnlockMutex(gLock);
    }
};

Voice* V(void* h)
{
    return (Voice*)h;
}
} // namespace

DXDEC S32 AILCALL AIL_startup(void)
{
    if (!gLock)
    {
        gLock = SDL_CreateMutex();
    }
    return 1;
}

DXDEC void AILCALL AIL_shutdown(void)
{
}

DXDEC S32 AILCALL AIL_set_preference(U32, S32 value)
{
    return value;
}

DXDEC S32 AILCALL AIL_get_preference(U32)
{
    return 0;
}

DXDEC void AILCALL AIL_delay(S32 intervals)
{
    SDL_Delay(intervals * 1000 / 60); // Miles counts in 1/60 s
}

DXDEC void FAR* AILCALL AIL_mem_alloc_lock(U32 size)
{
    return malloc(size);
}

DXDEC void AILCALL AIL_mem_free_lock(void FAR* ptr)
{
    free(ptr);
}

// The game passes the output format it wants; the mixer works in float stereo at 44.1 kHz regardless.
DXDEC S32 AILCALL AIL_waveOutOpen(HDIGDRIVER FAR* drvr, LPHWAVEOUT FAR*, S32, LPWAVEFORMAT)
{
    AIL_startup();
    if (!gOut)
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            return -1;
        }
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, kOutRate};
        gOut = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Mix, NULL);
        if (!gOut)
        {
            return -1;
        }
        SDL_ResumeAudioStreamDevice(gOut);
    }
    *drvr = (HDIGDRIVER)gOut;
    return 0; // 0 = success, as waveOutOpen
}

DXDEC void AILCALL AIL_waveOutClose(HDIGDRIVER)
{
    if (gOut)
    {
        SDL_DestroyAudioStream(gOut);
        gOut = NULL;
    }
}

DXDEC S32 AILCALL AIL_digital_handle_release(HDIGDRIVER)
{
    return 1;
}

DXDEC S32 AILCALL AIL_digital_handle_reacquire(HDIGDRIVER)
{
    return 1;
}

DXDEC void AILCALL AIL_set_digital_master_volume(HDIGDRIVER, S32 master_volume)
{
    Locked l;
    gMasterVolume = master_volume;
}

// --- samples

DXDEC HSAMPLE AILCALL AIL_allocate_sample_handle(HDIGDRIVER)
{
    return (HSAMPLE)NewVoice(false);
}

DXDEC void AILCALL AIL_release_sample_handle(HSAMPLE S)
{
    FreeVoice(V(S));
}

DXDEC void AILCALL AIL_init_sample(HSAMPLE S)
{
    Locked l;
    Voice* v = V(S);
    v->playing = false;
    v->data = NULL;
    v->len = 0;
    v->format = DIG_F_MONO_16;
    v->sign = false;
    v->rate = 22050;
    v->volume = 127;
    v->pan = 64;
    v->loops = 1;
    v->loopStart = v->loopEnd = 0;
    v->pos = 0;
}

DXDEC void AILCALL AIL_set_sample_address(HSAMPLE S, void FAR* start, U32 len)
{
    Locked l;
    V(S)->data = (const Uint8*)start;
    V(S)->len = len;
    V(S)->pos = 0;
}

DXDEC void AILCALL AIL_set_sample_type(HSAMPLE S, S32 format, U32 flags)
{
    Locked l;
    V(S)->format = format;
    V(S)->sign = (flags & DIG_PCM_SIGN) != 0;
}

DXDEC void AILCALL AIL_start_sample(HSAMPLE S)
{
    Locked l;
    V(S)->pos = 0;
    V(S)->playing = true;
}

DXDEC void AILCALL AIL_end_sample(HSAMPLE S)
{
    Locked l;
    V(S)->playing = false;
}

DXDEC void AILCALL AIL_set_sample_playback_rate(HSAMPLE S, S32 playback_rate)
{
    Locked l;
    V(S)->rate = playback_rate;
}

DXDEC void AILCALL AIL_set_sample_volume(HSAMPLE S, S32 volume)
{
    Locked l;
    V(S)->volume = volume;
}

DXDEC void AILCALL AIL_set_sample_pan(HSAMPLE S, S32 pan)
{
    Locked l;
    V(S)->pan = pan;
}

DXDEC void AILCALL AIL_set_sample_loop_count(HSAMPLE S, S32 loop_count)
{
    Locked l;
    V(S)->loops = loop_count;
}

DXDEC void AILCALL AIL_set_sample_loop_block(HSAMPLE S, S32 loop_start_offset, S32 loop_end_offset)
{
    Locked l;
    V(S)->loopStart = loop_start_offset > 0 ? loop_start_offset : 0;
    V(S)->loopEnd = loop_end_offset > 0 ? loop_end_offset : 0;
}

DXDEC U32 AILCALL AIL_sample_status(HSAMPLE S)
{
    Locked l;
    return V(S)->playing ? SMP_PLAYING : SMP_DONE;
}

// --- streams

DXDEC HSTREAM AILCALL AIL_open_stream(HDIGDRIVER, char FAR* filename, S32)
{
    std::string path = filename;
    for (size_t i = 0; i < path.size(); i++)
    {
        path[i] = path[i] == '\\' ? '/' : path[i]; // the game's paths use backslashes
    }
    SDL_AudioSpec spec;
    Uint8* buf = NULL;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len))
    {
        return NULL;
    }
    // Convert to 16-bit at the file's rate and channel count, which the mixer reads directly.
    SDL_AudioSpec dst = {SDL_AUDIO_S16LE, spec.channels > 1 ? 2 : 1, spec.freq};
    Uint8* conv = NULL;
    int convLen = 0;
    const bool ok = SDL_ConvertAudioSamples(&spec, buf, (int)len, &dst, &conv, &convLen);
    SDL_free(buf);
    if (!ok)
    {
        return NULL;
    }
    Voice* v = NewVoice(true);
    Locked l;
    v->pcm.assign(conv, conv + convLen);
    SDL_free(conv);
    v->data = &v->pcm[0];
    v->len = (U32)v->pcm.size();
    v->format = DIG_F_16BITS_MASK | (dst.channels == 2 ? DIG_F_STEREO_MASK : 0);
    v->rate = dst.freq;
    return (HSTREAM)v;
}

DXDEC void AILCALL AIL_close_stream(HSTREAM stream)
{
    FreeVoice(V(stream));
}

DXDEC void AILCALL AIL_start_stream(HSTREAM stream)
{
    Locked l;
    V(stream)->pos = 0;
    V(stream)->playing = true;
}

DXDEC void AILCALL AIL_set_stream_volume(HSTREAM stream, S32 volume)
{
    Locked l;
    V(stream)->volume = volume;
}

DXDEC S32 AILCALL AIL_stream_volume(HSTREAM stream)
{
    Locked l;
    return V(stream)->volume;
}

DXDEC void AILCALL AIL_set_stream_playback_rate(HSTREAM stream, S32 rate)
{
    Locked l;
    V(stream)->rate = rate;
}

DXDEC S32 AILCALL AIL_stream_playback_rate(HSTREAM stream)
{
    Locked l;
    return V(stream)->rate;
}

DXDEC void AILCALL AIL_set_stream_loop_count(HSTREAM stream, S32 count)
{
    Locked l;
    V(stream)->loops = count;
}

DXDEC S32 AILCALL AIL_stream_status(HSTREAM stream)
{
    Locked l;
    return V(stream)->playing ? SMP_PLAYING : SMP_DONE;
}

DXDEC void AILCALL AIL_set_stream_ms_position(HSTREAM S, S32 milliseconds)
{
    Locked l;
    V(S)->pos = (double)milliseconds * V(S)->rate / 1000.0;
}

DXDEC void AILCALL AIL_stream_ms_position(HSTREAM S, S32 FAR* total_milliseconds, S32 FAR* current_milliseconds)
{
    Locked l;
    const Voice& v = *V(S);
    const double frames = (double)v.len / BytesPerFrame(v);
    if (total_milliseconds)
    {
        *total_milliseconds = v.rate > 0 ? (S32)(frames * 1000.0 / v.rate) : 0;
    }
    if (current_milliseconds)
    {
        *current_milliseconds = v.rate > 0 ? (S32)(v.pos * 1000.0 / v.rate) : 0;
    }
}

// --- 3D: no provider, so the game uses its 2D path and none of these get a real handle

DXDEC S32 AILCALL AIL_enumerate_3D_providers(HPROENUM FAR*, HPROVIDER FAR*, C8 FAR* FAR*)
{
    return 0;
}

DXDEC M3DRESULT AILCALL AIL_open_3D_provider(HPROVIDER)
{
    return M3D_NOT_ENABLED;
}

DXDEC void AILCALL AIL_close_3D_provider(HPROVIDER)
{
}

DXDEC void AILCALL AIL_3D_provider_attribute(HPROVIDER, C8 FAR*, void FAR*)
{
}

DXDEC void AILCALL AIL_set_3D_provider_preference(HPROVIDER, C8 FAR*, void FAR*)
{
}

DXDEC H3DSAMPLE AILCALL AIL_allocate_3D_sample_handle(HPROVIDER)
{
    return NULL;
}

DXDEC void AILCALL AIL_release_3D_sample_handle(H3DSAMPLE)
{
}

DXDEC void AILCALL AIL_start_3D_sample(H3DSAMPLE)
{
}

DXDEC void AILCALL AIL_end_3D_sample(H3DSAMPLE)
{
}

DXDEC S32 AILCALL AIL_set_3D_sample_info(H3DSAMPLE, AILSOUNDINFO FAR*)
{
    return 0;
}

DXDEC void AILCALL AIL_set_3D_sample_volume(H3DSAMPLE, S32)
{
}

DXDEC void AILCALL AIL_set_3D_sample_playback_rate(H3DSAMPLE, S32)
{
}

DXDEC void AILCALL AIL_set_3D_sample_loop_count(H3DSAMPLE, U32)
{
}

DXDEC void AILCALL AIL_set_3D_sample_loop_block(H3DSAMPLE, S32, S32)
{
}

DXDEC U32 AILCALL AIL_3D_sample_status(H3DSAMPLE)
{
    return SMP_DONE;
}

DXDEC void AILCALL AIL_set_3D_sample_float_distances(H3DSAMPLE, F32, F32, F32, F32)
{
}

DXDEC void AILCALL AIL_set_3D_position(H3DPOBJECT, F32, F32, F32)
{
}
