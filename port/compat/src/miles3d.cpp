// Miles' 3D provider API (the subset GTA2 uses) on OpenAL Soft, loaded at run time. OpenAL renders
// through a loopback device into the SDL mix of miles.cpp, so it never opens an audio device itself.
// Without the library no provider is listed and the game keeps its 2D path.
#include "types.hpp"
#include "3rdParty/mss.h"
#include "miles3d.hpp"
#include "port_platform.hpp"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <mutex>
#include <vector>

#if defined(GTA2_HAVE_OPENAL)

#define AL_NO_PROTOTYPES
#define ALC_NO_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <AL/efx-presets.h>

namespace
{
// The game only opens a provider whose name starts like this (cSampleManager::Init3DProvider).
const char kProviderName[] = "Microsoft DirectSound3D hardware support";
const HPROVIDER kProvider = 1;
const S32 kMaxSamples = 16;

#define GTA2_AL_FUNCS(X)                                       \
    X(LPALCCREATECONTEXT, alcCreateContext)                    \
    X(LPALCMAKECONTEXTCURRENT, alcMakeContextCurrent)          \
    X(LPALCDESTROYCONTEXT, alcDestroyContext)                  \
    X(LPALCCLOSEDEVICE, alcCloseDevice)                        \
    X(LPALCGETPROCADDRESS, alcGetProcAddress)                  \
    X(LPALCISEXTENSIONPRESENT, alcIsExtensionPresent)          \
    X(LPALGETPROCADDRESS, alGetProcAddress)                    \
    X(LPALGENSOURCES, alGenSources)                            \
    X(LPALDELETESOURCES, alDeleteSources)                      \
    X(LPALGENBUFFERS, alGenBuffers)                            \
    X(LPALDELETEBUFFERS, alDeleteBuffers)                      \
    X(LPALBUFFERDATA, alBufferData)                            \
    X(LPALBUFFERIV, alBufferiv)                                \
    X(LPALSOURCEI, alSourcei)                                  \
    X(LPALSOURCE3I, alSource3i)                                \
    X(LPALSOURCEF, alSourcef)                                  \
    X(LPALSOURCE3F, alSource3f)                                \
    X(LPALSOURCEPLAY, alSourcePlay)                            \
    X(LPALSOURCESTOP, alSourceStop)                            \
    X(LPALGETSOURCEI, alGetSourcei)                            \
    X(LPALLISTENERF, alListenerf)                              \
    X(LPALDISTANCEMODEL, alDistanceModel)                      \
    X(LPALGETERROR, alGetError)

#define GTA2_AL_EXT_FUNCS(X)                                              \
    X(LPALCLOOPBACKOPENDEVICESOFT, alcLoopbackOpenDeviceSOFT, alc)        \
    X(LPALCISRENDERFORMATSUPPORTEDSOFT, alcIsRenderFormatSupportedSOFT, alc) \
    X(LPALCRENDERSAMPLESSOFT, alcRenderSamplesSOFT, alc)

#define GTA2_EFX_FUNCS(X)                                       \
    X(LPALGENEFFECTS, alGenEffects)                             \
    X(LPALDELETEEFFECTS, alDeleteEffects)                       \
    X(LPALEFFECTI, alEffecti)                                   \
    X(LPALEFFECTF, alEffectf)                                   \
    X(LPALEFFECTFV, alEffectfv)                                 \
    X(LPALGENAUXILIARYEFFECTSLOTS, alGenAuxiliaryEffectSlots)   \
    X(LPALDELETEAUXILIARYEFFECTSLOTS, alDeleteAuxiliaryEffectSlots) \
    X(LPALAUXILIARYEFFECTSLOTI, alAuxiliaryEffectSloti)

struct AL
{
#define GTA2_DECLARE(type, name) type name;
#define GTA2_DECLARE_EXT(type, name, from) type name;
    GTA2_AL_FUNCS(GTA2_DECLARE)
    GTA2_AL_EXT_FUNCS(GTA2_DECLARE_EXT)
    GTA2_EFX_FUNCS(GTA2_DECLARE)
#undef GTA2_DECLARE
#undef GTA2_DECLARE_EXT
};

AL al;
bool sLoadTried;
bool sLoaded;
ALCdevice* sDevice;
ALCcontext* sContext;
bool sOpen; // the game's provider is open; the context outlives it, so reopening is instant
bool sHaveEfx;
ALuint sEffect;
ALuint sSlot;
S32 sEnvironment = 17; // EAX_ENVIRONMENT_MOUNTAINS, the game's choice
float sMasterGain = 1.0f;

struct Sample3D
{
    ALuint mSource;
    ALuint mBuffer;
    S32 mBytesPerFrame;
    S32 mRate;
    U32 mFrames;
    U32 mLoopCount;
    S32 mLoopStart;
    S32 mLoopEnd;
    U32 mPlaysLeft;    // loop counts above 1: plays still to start, counted on wraps
    ALint mLastOffset; // sample offset at the previous render, to see the wraps
    S32 mVolume;
    float mPlaneDistance;
    float mMinDistance;
    float mMaxDistance;
};

// Where the listener hears from: this many world blocks above the plane of the sounds (see
// AIL_set_3D_position).
const float kListenerHeight = 5.0f;

std::recursive_mutex& Lock()
{
    static std::recursive_mutex sLock;
    return sLock;
}

std::vector<Sample3D*>& Samples()
{
    static std::vector<Sample3D*> sSamples;
    return sSamples;
}

#define LOCK std::lock_guard<std::recursive_mutex> lock(Lock())

// The library next to the executable first, then the system's.
SDL_SharedObject* OpenLibrary()
{
#if defined(_WIN32)
    const char* const kNames[] = {"OpenAL32.dll", "soft_oal.dll"};
#elif defined(__APPLE__)
    // In reG2.app the base path is Contents/Resources, the libraries are in Contents/Frameworks.
    const char* const kNames[] = {"../Frameworks/libopenal.1.dylib", "libopenal.1.dylib", "libopenal.dylib"};
#else
    const char* const kNames[] = {"libopenal.so.1", "libopenal.so"};
#endif
    const char* base = SDL_GetBasePath();
    for (const char* name : kNames)
    {
        if (base)
        {
            char path[1024];
            SDL_snprintf(path, sizeof(path), "%s%s", base, name);
            if (SDL_SharedObject* pLib = SDL_LoadObject(path))
            {
                return pLib;
            }
        }
        if (SDL_SharedObject* pLib = SDL_LoadObject(name))
        {
            return pLib;
        }
    }
    return NULL;
}

bool Load()
{
    if (sLoadTried)
    {
        return sLoaded;
    }
    sLoadTried = true;
    if (getenv("GTA2_NO_OPENAL"))
    {
        return false;
    }
    SDL_SharedObject* pLib = OpenLibrary();
    if (!pLib)
    {
        return false;
    }
#define GTA2_LOAD(type, name)                                                    \
    al.name = reinterpret_cast<type>(SDL_LoadFunction(pLib, #name));             \
    if (!al.name)                                                                \
    {                                                                            \
        return false;                                                            \
    }
    GTA2_AL_FUNCS(GTA2_LOAD)
#undef GTA2_LOAD
    if (!al.alcIsExtensionPresent(NULL, "ALC_SOFT_loopback"))
    {
        return false;
    }
#define GTA2_LOAD_EXT(type, name, from)                                          \
    al.name = reinterpret_cast<type>(al.alcGetProcAddress(NULL, #name));         \
    if (!al.name)                                                                \
    {                                                                            \
        return false;                                                            \
    }
    GTA2_AL_EXT_FUNCS(GTA2_LOAD_EXT)
#undef GTA2_LOAD_EXT
    sLoaded = true;
    return true;
}

// EAX environments in their order, as EFX reverb presets.
const EFXEAXREVERBPROPERTIES kEaxPresets[26] = {
    EFX_REVERB_PRESET_GENERIC, EFX_REVERB_PRESET_PADDEDCELL, EFX_REVERB_PRESET_ROOM,
    EFX_REVERB_PRESET_BATHROOM, EFX_REVERB_PRESET_LIVINGROOM, EFX_REVERB_PRESET_STONEROOM,
    EFX_REVERB_PRESET_AUDITORIUM, EFX_REVERB_PRESET_CONCERTHALL, EFX_REVERB_PRESET_CAVE,
    EFX_REVERB_PRESET_ARENA, EFX_REVERB_PRESET_HANGAR, EFX_REVERB_PRESET_CARPETEDHALLWAY,
    EFX_REVERB_PRESET_HALLWAY, EFX_REVERB_PRESET_STONECORRIDOR, EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_FOREST, EFX_REVERB_PRESET_CITY, EFX_REVERB_PRESET_MOUNTAINS,
    EFX_REVERB_PRESET_QUARRY, EFX_REVERB_PRESET_PLAIN, EFX_REVERB_PRESET_PARKINGLOT,
    EFX_REVERB_PRESET_SEWERPIPE, EFX_REVERB_PRESET_UNDERWATER, EFX_REVERB_PRESET_DRUGGED,
    EFX_REVERB_PRESET_DIZZY, EFX_REVERB_PRESET_PSYCHOTIC,
};

void ApplyEnvironment()
{
    if (!sHaveEfx)
    {
        return;
    }
    const EFXEAXREVERBPROPERTIES& r = kEaxPresets[sEnvironment];
    al.alEffectf(sEffect, AL_EAXREVERB_DENSITY, r.flDensity);
    al.alEffectf(sEffect, AL_EAXREVERB_DIFFUSION, r.flDiffusion);
    al.alEffectf(sEffect, AL_EAXREVERB_GAIN, r.flGain);
    al.alEffectf(sEffect, AL_EAXREVERB_GAINHF, r.flGainHF);
    al.alEffectf(sEffect, AL_EAXREVERB_GAINLF, r.flGainLF);
    al.alEffectf(sEffect, AL_EAXREVERB_DECAY_TIME, r.flDecayTime);
    al.alEffectf(sEffect, AL_EAXREVERB_DECAY_HFRATIO, r.flDecayHFRatio);
    al.alEffectf(sEffect, AL_EAXREVERB_DECAY_LFRATIO, r.flDecayLFRatio);
    al.alEffectf(sEffect, AL_EAXREVERB_REFLECTIONS_GAIN, r.flReflectionsGain);
    al.alEffectf(sEffect, AL_EAXREVERB_REFLECTIONS_DELAY, r.flReflectionsDelay);
    al.alEffectfv(sEffect, AL_EAXREVERB_REFLECTIONS_PAN, r.flReflectionsPan);
    al.alEffectf(sEffect, AL_EAXREVERB_LATE_REVERB_GAIN, r.flLateReverbGain);
    al.alEffectf(sEffect, AL_EAXREVERB_LATE_REVERB_DELAY, r.flLateReverbDelay);
    al.alEffectfv(sEffect, AL_EAXREVERB_LATE_REVERB_PAN, r.flLateReverbPan);
    al.alEffectf(sEffect, AL_EAXREVERB_ECHO_TIME, r.flEchoTime);
    al.alEffectf(sEffect, AL_EAXREVERB_ECHO_DEPTH, r.flEchoDepth);
    al.alEffectf(sEffect, AL_EAXREVERB_MODULATION_TIME, r.flModulationTime);
    al.alEffectf(sEffect, AL_EAXREVERB_MODULATION_DEPTH, r.flModulationDepth);
    al.alEffectf(sEffect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, r.flAirAbsorptionGainHF);
    al.alEffectf(sEffect, AL_EAXREVERB_HFREFERENCE, r.flHFReference);
    al.alEffectf(sEffect, AL_EAXREVERB_LFREFERENCE, r.flLFReference);
    al.alEffectf(sEffect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, r.flRoomRolloffFactor);
    al.alEffecti(sEffect, AL_EAXREVERB_DECAY_HFLIMIT, r.iDecayHFLimit);
    al.alAuxiliaryEffectSloti(sSlot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(sEffect));
}

bool OpenDevice()
{
    if (sContext)
    {
        sOpen = true;
        return true;
    }
    if (!Load())
    {
        return false;
    }
    sDevice = al.alcLoopbackOpenDeviceSOFT(NULL);
    if (!sDevice)
    {
        return false;
    }
    if (!al.alcIsRenderFormatSupportedSOFT(sDevice, Miles3D_kRate, ALC_STEREO_SOFT, ALC_FLOAT_SOFT))
    {
        al.alcCloseDevice(sDevice);
        sDevice = NULL;
        return false;
    }
    // GTA2_HRTF=1: binaural rendering for headphones instead of stereo panning.
    const ALCint attrs[] = {ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
                            ALC_FREQUENCY, Miles3D_kRate, ALC_HRTF_SOFT, getenv("GTA2_HRTF") ? ALC_TRUE : ALC_FALSE, 0};
    sContext = al.alcCreateContext(sDevice, attrs);
    if (!sContext || !al.alcMakeContextCurrent(sContext))
    {
        if (sContext)
        {
            al.alcDestroyContext(sContext);
        }
        al.alcCloseDevice(sDevice);
        sContext = NULL;
        sDevice = NULL;
        return false;
    }
    // DirectSound3D's model: full volume up to the minimum distance, 1/distance beyond, constant past the maximum.
    al.alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    al.alListenerf(AL_GAIN, sMasterGain);

    sHaveEfx = false;
    if (al.alcIsExtensionPresent(sDevice, "ALC_EXT_EFX"))
    {
        sHaveEfx = true;
#define GTA2_LOAD_EFX(type, name)                                                \
    al.name = reinterpret_cast<type>(al.alGetProcAddress(#name));                \
    sHaveEfx = sHaveEfx && al.name;
        GTA2_EFX_FUNCS(GTA2_LOAD_EFX)
#undef GTA2_LOAD_EFX
    }
    if (sHaveEfx)
    {
        al.alGetError();
        al.alGenEffects(1, &sEffect);
        al.alEffecti(sEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        al.alGenAuxiliaryEffectSlots(1, &sSlot);
        sHaveEfx = al.alGetError() == AL_NO_ERROR;
        ApplyEnvironment();
    }
    SDL_Log("3D sound: OpenAL Soft%s", sHaveEfx ? " with EAX reverb" : "");
    sOpen = true;
    return true;
}

// The game closes the provider when 3D sound is turned off: its samples go, the context stays.
void CloseDevice()
{
    for (Sample3D* s : Samples())
    {
        al.alDeleteSources(1, &s->mSource);
        al.alDeleteBuffers(1, &s->mBuffer);
        delete s;
    }
    Samples().clear();
    sOpen = false;
}

Sample3D* S3(H3DSAMPLE h)
{
    return reinterpret_cast<Sample3D*>(h);
}

// Loop points and looping are buffer and source state, set while the source is stopped.
void ApplyLoop(Sample3D* s)
{
    ALint state = AL_STOPPED;
    al.alGetSourcei(s->mSource, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING || !s->mFrames)
    {
        return;
    }
    const ALint start = SDL_clamp(s->mLoopStart / s->mBytesPerFrame, 0, static_cast<ALint>(s->mFrames - 1));
    ALint end = s->mLoopEnd < 0 ? static_cast<ALint>(s->mFrames) : s->mLoopEnd / s->mBytesPerFrame;
    end = SDL_clamp(end, start + 1, static_cast<ALint>(s->mFrames));
    const ALint points[2] = {start, end};
    al.alSourcei(s->mSource, AL_BUFFER, 0);
    al.alBufferiv(s->mBuffer, AL_LOOP_POINTS_SOFT, points);
    al.alSourcei(s->mSource, AL_BUFFER, static_cast<ALint>(s->mBuffer));
    al.alSourcei(s->mSource, AL_LOOPING, s->mLoopCount == 1 ? AL_FALSE : AL_TRUE);
    s->mPlaysLeft = s->mLoopCount;
    s->mLastOffset = 0;
}

// OpenAL loops forever: a count of N plays stops looping once the last play has begun.
void CountLoops()
{
    for (Sample3D* s : Samples())
    {
        if (s->mPlaysLeft <= 1)
        {
            continue;
        }
        ALint state = AL_STOPPED;
        ALint offset = 0;
        al.alGetSourcei(s->mSource, AL_SOURCE_STATE, &state);
        al.alGetSourcei(s->mSource, AL_SAMPLE_OFFSET, &offset);
        if (state != AL_PLAYING)
        {
            continue;
        }
        if (offset < s->mLastOffset && --s->mPlaysLeft == 1)
        {
            al.alSourcei(s->mSource, AL_LOOPING, AL_FALSE);
        }
        s->mLastOffset = offset;
    }
}
// DirectSound3D's attenuation (inverse distance from the minimum, constant past the maximum) on the
// distance in the plane, with the game's volume; OpenAL only places the sound.
void ApplyGain(Sample3D* s)
{
    const float d = SDL_clamp(s->mPlaneDistance, s->mMinDistance, s->mMaxDistance);
    al.alSourcef(s->mSource, AL_GAIN, SDL_clamp(s->mVolume, 0, 127) / 127.0f * (s->mMinDistance / d));
}
} // namespace

bool Miles3D_Render(float* pMix, int frames)
{
    LOCK;
    if (!sContext || !sOpen)
    {
        return false;
    }
    static std::vector<float> sBuf;
    sBuf.resize(static_cast<size_t>(frames) * 2);
    // Small slices, so that a short loop cannot wrap twice between two counts.
    const int kSlice = 128;
    for (int done = 0; done < frames; done += kSlice)
    {
        CountLoops();
        al.alcRenderSamplesSOFT(sDevice, sBuf.data() + done * 2, SDL_min(kSlice, frames - done));
    }
    for (size_t i = 0; i < sBuf.size(); i++)
    {
        pMix[i] += sBuf[i];
    }
    return true;
}

void Miles3D_SetMasterVolume(S32 volume)
{
    LOCK;
    sMasterGain = volume / 127.0f;
    if (sContext)
    {
        al.alListenerf(AL_GAIN, sMasterGain);
    }
}

DXDEC S32 AILCALL AIL_enumerate_3D_providers(HPROENUM FAR* next, HPROVIDER FAR* dest, C8 FAR* FAR* name)
{
    LOCK;
    if (*next != 0 || !Load())
    {
        return 0;
    }
    *next = 1;
    *dest = kProvider;
    *name = const_cast<C8*>(kProviderName);
    return 1;
}

DXDEC M3DRESULT AILCALL AIL_open_3D_provider(HPROVIDER lib)
{
    LOCK;
    return lib == kProvider && OpenDevice() ? M3D_NOERR : M3D_NOT_ENABLED;
}

DXDEC void AILCALL AIL_close_3D_provider(HPROVIDER)
{
    LOCK;
    CloseDevice();
}

// Values are 32-bit, as Miles writes them.
DXDEC void AILCALL AIL_3D_provider_attribute(HPROVIDER, C8 FAR* name, void FAR* val)
{
    LOCK;
    s32 value = -1;
    if (strcmp(name, "Maximum supported samples") == 0)
    {
        value = kMaxSamples;
    }
    else if (strcmp(name, "EAX environment selection") == 0)
    {
        value = sHaveEfx ? sEnvironment : -1;
    }
    memcpy(val, &value, sizeof(value));
}

DXDEC void AILCALL AIL_set_3D_provider_preference(HPROVIDER, C8 FAR* name, void FAR* val)
{
    LOCK;
    if (strcmp(name, "EAX environment selection") == 0)
    {
        s32 env;
        memcpy(&env, val, sizeof(env));
        if (env >= 0 && env < 26)
        {
            sEnvironment = env;
            ApplyEnvironment();
        }
    }
}

DXDEC H3DSAMPLE AILCALL AIL_allocate_3D_sample_handle(HPROVIDER)
{
    LOCK;
    if (!sOpen || static_cast<S32>(Samples().size()) >= kMaxSamples)
    {
        return NULL;
    }
    Sample3D* s = new Sample3D();
    al.alGenSources(1, &s->mSource);
    al.alGenBuffers(1, &s->mBuffer);
    s->mBytesPerFrame = 2;
    s->mLoopCount = 1;
    s->mLoopEnd = -1;
    s->mVolume = 127;
    s->mMinDistance = 1.0f;
    s->mMaxDistance = 1.0f;
    al.alSourcef(s->mSource, AL_ROLLOFF_FACTOR, 0.0f);
    if (sHaveEfx)
    {
        al.alSource3i(s->mSource, AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(sSlot), 0, AL_FILTER_NULL);
    }
    Samples().push_back(s);
    return reinterpret_cast<H3DSAMPLE>(s);
}

DXDEC void AILCALL AIL_release_3D_sample_handle(H3DSAMPLE h)
{
    LOCK;
    Sample3D* s = S3(h);
    auto it = std::find(Samples().begin(), Samples().end(), s);
    if (it == Samples().end())
    {
        return;
    }
    Samples().erase(it);
    al.alDeleteSources(1, &s->mSource);
    al.alDeleteBuffers(1, &s->mBuffer);
    delete s;
}

DXDEC void AILCALL AIL_start_3D_sample(H3DSAMPLE h)
{
    LOCK;
    Sample3D* s = S3(h);
    al.alSourceStop(s->mSource);
    ApplyLoop(s);
    al.alSourcePlay(s->mSource);
}

DXDEC void AILCALL AIL_end_3D_sample(H3DSAMPLE h)
{
    LOCK;
    al.alSourceStop(S3(h)->mSource);
}

// The game's samples: mono PCM in its sound bank.
DXDEC S32 AILCALL AIL_set_3D_sample_info(H3DSAMPLE h, AILSOUNDINFO FAR* info)
{
    LOCK;
    Sample3D* s = S3(h);
    if (info->channels != 1 || (info->bits != 8 && info->bits != 16) || !info->rate)
    {
        return 0;
    }
    al.alSourceStop(s->mSource);
    al.alSourcei(s->mSource, AL_BUFFER, 0);
    s->mBytesPerFrame = info->bits / 8;
    s->mRate = static_cast<S32>(info->rate);
    s->mFrames = info->data_len / s->mBytesPerFrame;
    s->mLoopCount = 1;
    s->mLoopStart = 0;
    s->mLoopEnd = -1;
    al.alGetError();
    al.alBufferData(s->mBuffer, info->bits == 16 ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8, info->data_ptr,
                    static_cast<ALsizei>(s->mFrames * s->mBytesPerFrame), s->mRate);
    al.alSourcei(s->mSource, AL_BUFFER, static_cast<ALint>(s->mBuffer));
    al.alSourcef(s->mSource, AL_PITCH, 1.0f);
    return al.alGetError() == AL_NO_ERROR ? 1 : 0;
}

DXDEC void AILCALL AIL_set_3D_sample_volume(H3DSAMPLE h, S32 volume)
{
    LOCK;
    S3(h)->mVolume = volume;
    ApplyGain(S3(h));
}

DXDEC void AILCALL AIL_set_3D_sample_playback_rate(H3DSAMPLE h, S32 playback_rate)
{
    LOCK;
    Sample3D* s = S3(h);
    if (s->mRate > 0 && playback_rate > 0)
    {
        al.alSourcef(s->mSource, AL_PITCH, SDL_clamp(static_cast<float>(playback_rate) / s->mRate, 0.05f, 8.0f));
    }
}

// 0 loops forever, 1 plays once.
DXDEC void AILCALL AIL_set_3D_sample_loop_count(H3DSAMPLE h, U32 loops)
{
    LOCK;
    S3(h)->mLoopCount = loops;
    ApplyLoop(S3(h));
}

DXDEC void AILCALL AIL_set_3D_sample_loop_block(H3DSAMPLE h, S32 loop_start_offset, S32 loop_end_offset)
{
    LOCK;
    S3(h)->mLoopStart = loop_start_offset;
    S3(h)->mLoopEnd = loop_end_offset;
    ApplyLoop(S3(h));
}

DXDEC U32 AILCALL AIL_3D_sample_status(H3DSAMPLE h)
{
    LOCK;
    ALint state = AL_STOPPED;
    al.alGetSourcei(S3(h)->mSource, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING ? SMP_PLAYING : SMP_DONE;
}

DXDEC void AILCALL
AIL_3D_sample_float_distances(H3DSAMPLE, F32 FAR* max_front_dist, F32 FAR* min_front_dist, F32 FAR* max_back_dist, F32 FAR* min_back_dist)
{
}

// Miles uses DirectSound3D's left-handed axes (+z ahead); OpenAL's are right-handed (-z ahead).
// The game gives positions in the plane of the listener (y = 0), which is the player's ped or car.
// Heard from that plane, a sound beside the player is panned all the way to one side and the
// player's own sounds, a hair away, follow the direction of that hair; heard from above, like the
// top-down camera, the pan grows with the distance on screen as the game's 2D pan does.
DXDEC void AILCALL AIL_set_3D_position(H3DPOBJECT obj, F32 X, F32 Y, F32 Z)
{
    LOCK;
    Sample3D* s = S3(reinterpret_cast<H3DSAMPLE>(obj));
    al.alSource3f(s->mSource, AL_POSITION, X, Y - kListenerHeight, -Z);
    s->mPlaneDistance = sqrtf(X * X + Z * Z);
    ApplyGain(s);
}

DXDEC void AILCALL
AIL_set_3D_sample_float_distances(H3DSAMPLE h, F32 max_front_dist, F32 min_front_dist, F32, F32)
{
    LOCK;
    Sample3D* s = S3(h);
    s->mMinDistance = min_front_dist > 0.0f ? min_front_dist : 1.0f;
    s->mMaxDistance = max_front_dist > s->mMinDistance ? max_front_dist : s->mMinDistance;
    ApplyGain(s);
}

#else

bool Miles3D_Render(float*, int)
{
    return false;
}

void Miles3D_SetMasterVolume(S32)
{
}

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

DXDEC void AILCALL AIL_3D_provider_attribute(HPROVIDER, C8 FAR*, void FAR* val)
{
    const s32 value = -1;
    memcpy(val, &value, sizeof(value));
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

DXDEC void AILCALL
AIL_3D_sample_float_distances(H3DSAMPLE, F32 FAR*, F32 FAR*, F32 FAR*, F32 FAR*)
{
}

DXDEC void AILCALL AIL_set_3D_position(H3DPOBJECT, F32, F32, F32)
{
}

DXDEC void AILCALL AIL_set_3D_sample_float_distances(H3DSAMPLE, F32, F32, F32, F32)
{
}

#endif
