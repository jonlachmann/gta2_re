#include "present_log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <deque>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#if defined(__linux__)
#include <poll.h>
#include <time.h>
#endif

namespace
{
// One swap. Kinds: G a frame drawn by the game loop, R the last frame shown again, O anything else (menus, loading).
struct Entry
{
    char mKind;
    unsigned long long mGameNs;
    unsigned long long mSwapCallNs;
    unsigned long long mSwapReturnNs;
    unsigned long long mPresentedNs; // 0: never shown
    unsigned long long mMsc;
    unsigned mRefreshNs;
    unsigned mFlags; // wp_presentation_feedback.kind: 1 vsync, 2 hw clock, 4 hw completion, 8 zero copy
    unsigned long long mGpuBeginNs; // when the GPU reached the frame's first and last commands, on our clock; 0 unknown
    unsigned long long mGpuEndNs;
    int mGpuSlot; // its timer queries, -1 none
    bool mDone;   // presentation known
};

// Totals over the game frames shown since the last summary.
struct Summary
{
    std::vector<double> mIntervalMs; // from the previous game frame's presentation
    std::vector<double> mErrorMs;    // that interval against the game time between the two frames
    unsigned mShown;
    unsigned mRepeats;
    unsigned mDiscarded;
    unsigned mVsync;
    unsigned mZeroCopy;
    unsigned mRefreshNs;
    std::vector<double> mGpuMs;          // GPU time of each game frame
    std::vector<double> mGpuDoneToShown; // from the GPU finishing a game frame to its presentation
    unsigned long long mStartNs;
};

FILE* spFile;
SDL_Window* spWindow;
bool sFromCompositor;
int sClockId = 1; // CLOCK_MONOTONIC until the compositor names its clock
std::deque<Entry> sPending;
unsigned long long sNextSeq; // of the next swap; sPending holds the last sPending.size() ones
char sNextKind = 'O';
unsigned long long sNextGameNs;
bool sHasPrev;
Entry sPrev;
Summary sLast10s;
Summary sWholeRun;

unsigned long long NowNs()
{
#if defined(__linux__)
    timespec ts;
    clock_gettime(sClockId, &ts);
    return static_cast<unsigned long long>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
#else
    return SDL_GetTicksNS();
#endif
}

// GPU timestamps around each frame (GL_EXT_disjoint_timer_query, or timer queries on desktop GL).
const int kGpuSlots = 512; // more than the swaps that can wait in sPending
struct
{
    void (*GenQueries)(int, unsigned*);
    void (*QueryCounter)(unsigned, unsigned);
    void (*GetQueryObjectuiv)(unsigned, unsigned, unsigned*);
    void (*GetQueryObjectui64v)(unsigned, unsigned, unsigned long long*);
    void (*GetInteger64v)(unsigned, long long*);
} gpu;
const unsigned GL_TIMESTAMP = 0x8E28, GL_QUERY_RESULT = 0x8866, GL_QUERY_RESULT_AVAILABLE = 0x8867;
bool sGpu;
unsigned sGpuQueries[kGpuSlots * 2];
int sGpuBeginSlot = -1; // issued after the last swap, for the next frame
long long sGpuOffsetNs; // GPU clock minus ours
unsigned long long sGpuCalibratedNs;

bool HasGlExtension(const char* pName)
{
    typedef const unsigned char* (*GetStringi)(unsigned, unsigned);
    typedef void (*GetIntegerv)(unsigned, int*);
    GetStringi getStringi = reinterpret_cast<GetStringi>(SDL_GL_GetProcAddress("glGetStringi"));
    GetIntegerv getIntegerv = reinterpret_cast<GetIntegerv>(SDL_GL_GetProcAddress("glGetIntegerv"));
    int n = 0;
    if (getStringi && getIntegerv)
    {
        getIntegerv(0x821D /* GL_NUM_EXTENSIONS */, &n);
    }
    for (int i = 0; i < n; i++)
    {
        const char* pExt = reinterpret_cast<const char*>(getStringi(0x1F03 /* GL_EXTENSIONS */, i));
        if (pExt && strcmp(pExt, pName) == 0)
        {
            return true;
        }
    }
    return false;
}

template <typename T> bool LoadGl(T* pOut, const char* pName, const char* pSuffix)
{
    char name[64];
    SDL_snprintf(name, sizeof(name), "%s%s", pName, pSuffix);
    *pOut = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return *pOut != NULL;
}

void AttachGpu()
{
    int profile = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
    const bool es = profile == SDL_GL_CONTEXT_PROFILE_ES;
    const char* pSuffix = es ? "EXT" : "";
    sGpu = (!es || HasGlExtension("GL_EXT_disjoint_timer_query")) && LoadGl(&gpu.GenQueries, "glGenQueries", pSuffix) &&
           LoadGl(&gpu.QueryCounter, "glQueryCounter", pSuffix) && LoadGl(&gpu.GetQueryObjectuiv, "glGetQueryObjectuiv", pSuffix) &&
           LoadGl(&gpu.GetQueryObjectui64v, "glGetQueryObjectui64v", pSuffix) && LoadGl(&gpu.GetInteger64v, "glGetInteger64v", pSuffix);
    if (sGpu)
    {
        gpu.GenQueries(kGpuSlots * 2, sGpuQueries);
    }
}

void CalibrateGpu(unsigned long long now)
{
    if (now - sGpuCalibratedNs < 1000000000ull)
    {
        return;
    }
    sGpuCalibratedNs = now;
    long long gpuNow = 0;
    gpu.GetInteger64v(GL_TIMESTAMP, &gpuNow);
    sGpuOffsetNs = gpuNow - static_cast<long long>(NowNs());
}

// Reads the entry's GPU times once both queries are done; true when nothing is left to wait for.
bool ReadGpu(Entry& e, bool wait)
{
    if (e.mGpuSlot < 0)
    {
        return true;
    }
    unsigned available = 0;
    gpu.GetQueryObjectuiv(sGpuQueries[e.mGpuSlot * 2 + 1], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available && !wait)
    {
        return false;
    }
    unsigned long long begin = 0;
    unsigned long long end = 0;
    gpu.GetQueryObjectui64v(sGpuQueries[e.mGpuSlot * 2], GL_QUERY_RESULT, &begin);
    gpu.GetQueryObjectui64v(sGpuQueries[e.mGpuSlot * 2 + 1], GL_QUERY_RESULT, &end);
    e.mGpuBeginNs = begin - sGpuOffsetNs;
    e.mGpuEndNs = end - sGpuOffsetNs;
    e.mGpuSlot = -1;
    return true;
}

unsigned DisplayModeRefreshNs()
{
    const SDL_DisplayMode* pMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(spWindow));
    return pMode && pMode->refresh_rate > 0.0f ? static_cast<unsigned>(1e9 / pMode->refresh_rate + 0.5) : 0;
}

double Percentile(std::vector<double>& v, double p)
{
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p / 100.0 * (v.size() - 1) + 0.5)];
}

void WriteSummary(Summary& s, const char* pWhat)
{
    if (s.mIntervalMs.empty())
    {
        return;
    }
    std::vector<double>& iv = s.mIntervalMs;
    std::vector<double>& err = s.mErrorMs;
    fprintf(spFile, "# %s: %u game frames, %u repeats, %u never shown, refresh %.3f ms\n", pWhat, (unsigned)iv.size(), s.mRepeats,
            s.mDiscarded, s.mRefreshNs / 1e6);
    if (sFromCompositor)
    {
        fprintf(spFile, "#   compositor: %.1f%% on vblank, %.1f%% scanned out directly\n", 100.0 * s.mVsync / s.mShown,
                100.0 * s.mZeroCopy / s.mShown);
    }
    fprintf(spFile, "#   frame to frame ms: p1 %.2f p5 %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f\n", Percentile(iv, 1), Percentile(iv, 5),
            Percentile(iv, 50), Percentile(iv, 95), Percentile(iv, 99), Percentile(iv, 100));
    fprintf(spFile, "#   shown minus game time ms: p50 %.2f p95 %.2f p99 %.2f max %.2f\n", Percentile(err, 50), Percentile(err, 95),
            Percentile(err, 99), Percentile(err, 100));
    if (!s.mGpuMs.empty())
    {
        std::vector<double>& g = s.mGpuMs;
        std::vector<double>& d = s.mGpuDoneToShown;
        fprintf(spFile, "#   GPU ms per game frame: p50 %.2f p95 %.2f p99 %.2f max %.2f; GPU done to shown ms: p5 %.2f p50 %.2f p95 %.2f\n",
                Percentile(g, 50), Percentile(g, 95), Percentile(g, 99), Percentile(g, 100), Percentile(d, 5), Percentile(d, 50),
                Percentile(d, 95));
    }
    if (s.mRefreshNs)
    {
        unsigned counts[8] = {};
        for (size_t i = 0; i < iv.size(); i++)
        {
            counts[std::min(7, static_cast<int>(floor(iv[i] * 1e6 / s.mRefreshNs + 0.5)))]++;
        }
        fprintf(spFile, "#   refreshes each frame was shown:");
        for (int i = 0; i < 8; i++)
        {
            if (counts[i])
            {
                fprintf(spFile, " %d%s: %.2f%%", i, i == 7 ? "+" : "", 100.0 * counts[i] / iv.size());
            }
        }
        fprintf(spFile, "\n");
    }
    fflush(spFile);
    const unsigned long long start = s.mStartNs;
    s = Summary();
    s.mStartNs = start;
}

void Add(Summary& s, const Entry& e)
{
    if (!e.mPresentedNs)
    {
        s.mDiscarded++;
        return;
    }
    s.mShown++;
    s.mVsync += (e.mFlags & 1) != 0;
    s.mZeroCopy += (e.mFlags & 8) != 0;
    s.mRepeats += e.mKind == 'R';
    if (e.mKind == 'G' && sHasPrev)
    {
        const double shownMs = (e.mPresentedNs - sPrev.mPresentedNs) / 1e6;
        const double gameMs = (static_cast<long long>(e.mGameNs) - static_cast<long long>(sPrev.mGameNs)) / 1e6;
        s.mIntervalMs.push_back(shownMs);
        s.mErrorMs.push_back(fabs(shownMs - gameMs));
        s.mRefreshNs = e.mRefreshNs;
    }
    if (e.mKind == 'G' && e.mGpuEndNs)
    {
        s.mGpuMs.push_back((e.mGpuEndNs - e.mGpuBeginNs) / 1e6);
        s.mGpuDoneToShown.push_back((static_cast<long long>(e.mPresentedNs) - static_cast<long long>(e.mGpuEndNs)) / 1e6);
    }
}

void Write(const Entry& e)
{
    fprintf(spFile, "%c %llu %llu %llu %llu %u %llu %u %llu %llu\n", e.mKind, e.mSwapCallNs, e.mSwapReturnNs, e.mPresentedNs, e.mMsc,
            e.mRefreshNs, e.mGameNs, e.mFlags, e.mGpuBeginNs, e.mGpuEndNs);
    Add(sLast10s, e);
    Add(sWholeRun, e);
    // Intervals run between game frames only: other frames in between break the chain.
    if (e.mKind == 'G' && e.mPresentedNs)
    {
        sHasPrev = true;
        sPrev = e;
    }
    else if (e.mKind == 'O')
    {
        sHasPrev = false;
    }
    if (e.mSwapReturnNs - sLast10s.mStartNs >= 10000000000ull)
    {
        WriteSummary(sLast10s, "last 10 s");
        sLast10s.mStartNs = e.mSwapReturnNs;
    }
}

// Writes the swaps whose presentation and GPU times are known, in order, and the oldest beyond `keep` in any case.
void Flush(size_t keep)
{
    while (!sPending.empty())
    {
        Entry& e = sPending.front();
        const bool force = sPending.size() > keep;
        if (!ReadGpu(e, force) || (!e.mDone && !force))
        {
            return;
        }
        Write(e);
        sPending.pop_front();
    }
}

void AtExit()
{
    Flush(0);
    WriteSummary(sWholeRun, "whole run");
    fclose(spFile);
    spFile = NULL;
}

#if defined(__linux__)
// wp_presentation feedback, through libwayland-client as SDL loaded it, on an event queue of our own.
struct wl_message
{
    const char* name;
    const char* signature;
    const struct wl_interface** types;
};
struct wl_interface
{
    const char* name;
    int version;
    int method_count;
    const wl_message* methods;
    int event_count;
    const wl_message* events;
};
struct
{
    void* (*display_create_queue)(void*);
    void* (*proxy_create_wrapper)(void*);
    void (*proxy_set_queue)(void*, void*);
    void* (*proxy_marshal_flags)(void*, uint32_t, const wl_interface*, uint32_t, uint32_t, ...);
    int (*proxy_add_listener)(void*, void (**)(void), void*);
    void (*proxy_destroy)(void*);
    uint32_t (*proxy_get_version)(void*);
    int (*display_roundtrip_queue)(void*, void*);
    int (*display_dispatch_queue_pending)(void*, void*);
    int (*display_prepare_read_queue)(void*, void*);
    int (*display_flush)(void*);
    int (*display_get_fd)(void*);
    int (*display_read_events)(void*);
    void (*display_cancel_read)(void*);
    const wl_interface* registry_interface;
    const wl_interface* surface_interface;
    const wl_interface* output_interface;
} wl;
void* spDisplay;
void* spQueue;
void* spSurface;
void* spPresentation;

const wl_interface* sNoTypes[8];
const wl_interface* sFeedbackTypes[2];
const wl_interface* sOutputTypes[1];
extern const wl_interface kFeedbackInterface;
const wl_message kPresentationRequests[] = {{"destroy", "", sNoTypes}, {"feedback", "on", sFeedbackTypes}};
const wl_message kPresentationEvents[] = {{"clock_id", "u", sNoTypes}};
const wl_interface kPresentationInterface = {"wp_presentation", 1, 2, kPresentationRequests, 1, kPresentationEvents};
const wl_message kFeedbackEvents[] = {{"sync_output", "o", sOutputTypes}, {"presented", "uuuuuuu", sNoTypes}, {"discarded", "", sNoTypes}};
const wl_interface kFeedbackInterface = {"wp_presentation_feedback", 1, 0, NULL, 3, kFeedbackEvents};

Entry* PendingEntry(void* pSeq)
{
    const unsigned long long seq = reinterpret_cast<uintptr_t>(pSeq);
    const unsigned long long first = sNextSeq - sPending.size();
    return seq >= first && seq < sNextSeq ? &sPending[static_cast<size_t>(seq - first)] : NULL;
}

void OnClockId(void*, void*, uint32_t clockId)
{
    sClockId = static_cast<int>(clockId);
}

void OnSyncOutput(void*, void*, void*)
{
}

void OnPresented(void* pSeq, void* pFeedback, uint32_t secHi, uint32_t secLo, uint32_t nsec, uint32_t refresh, uint32_t seqHi,
                 uint32_t seqLo, uint32_t flags)
{
    if (Entry* e = PendingEntry(pSeq))
    {
        e->mPresentedNs = ((static_cast<unsigned long long>(secHi) << 32) | secLo) * 1000000000ull + nsec;
        e->mMsc = (static_cast<unsigned long long>(seqHi) << 32) | seqLo;
        e->mRefreshNs = refresh;
        e->mFlags = flags;
        e->mDone = true;
    }
    wl.proxy_destroy(pFeedback);
}

void OnDiscarded(void* pSeq, void* pFeedback)
{
    if (Entry* e = PendingEntry(pSeq))
    {
        e->mDone = true;
    }
    wl.proxy_destroy(pFeedback);
}

void OnGlobal(void*, void* pRegistry, uint32_t name, const char* pInterface, uint32_t)
{
    if (!spPresentation && strcmp(pInterface, kPresentationInterface.name) == 0)
    {
        spPresentation = wl.proxy_marshal_flags(pRegistry, 0, &kPresentationInterface, 1, 0, name, kPresentationInterface.name, 1, NULL);
    }
}

void OnGlobalRemove(void*, void*, uint32_t)
{
}

typedef void (*Handler)(void);
Handler sRegistryListener[] = {reinterpret_cast<Handler>(OnGlobal), reinterpret_cast<Handler>(OnGlobalRemove)};
Handler sPresentationListener[] = {reinterpret_cast<Handler>(OnClockId)};
Handler sFeedbackListener[] = {reinterpret_cast<Handler>(OnSyncOutput), reinterpret_cast<Handler>(OnPresented),
                               reinterpret_cast<Handler>(OnDiscarded)};

template <typename T> bool Load(SDL_SharedObject* pLib, T* pOut, const char* pName)
{
    *pOut = reinterpret_cast<T>(SDL_LoadFunction(pLib, pName));
    return *pOut != NULL;
}

bool LoadWayland()
{
    SDL_SharedObject* pLib = SDL_LoadObject("libwayland-client.so.0");
    return pLib && Load(pLib, &wl.display_create_queue, "wl_display_create_queue") &&
           Load(pLib, &wl.proxy_create_wrapper, "wl_proxy_create_wrapper") && Load(pLib, &wl.proxy_set_queue, "wl_proxy_set_queue") &&
           Load(pLib, &wl.proxy_marshal_flags, "wl_proxy_marshal_flags") && Load(pLib, &wl.proxy_add_listener, "wl_proxy_add_listener") &&
           Load(pLib, &wl.proxy_destroy, "wl_proxy_destroy") && Load(pLib, &wl.proxy_get_version, "wl_proxy_get_version") &&
           Load(pLib, &wl.display_roundtrip_queue, "wl_display_roundtrip_queue") &&
           Load(pLib, &wl.display_dispatch_queue_pending, "wl_display_dispatch_queue_pending") &&
           Load(pLib, &wl.display_prepare_read_queue, "wl_display_prepare_read_queue") && Load(pLib, &wl.display_flush, "wl_display_flush") &&
           Load(pLib, &wl.display_get_fd, "wl_display_get_fd") && Load(pLib, &wl.display_read_events, "wl_display_read_events") &&
           Load(pLib, &wl.display_cancel_read, "wl_display_cancel_read") && Load(pLib, &wl.registry_interface, "wl_registry_interface") &&
           Load(pLib, &wl.surface_interface, "wl_surface_interface") && Load(pLib, &wl.output_interface, "wl_output_interface");
}

// True when the compositor will report presentation times.
bool AttachWayland()
{
    const SDL_PropertiesID props = SDL_GetWindowProperties(spWindow);
    spDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    spSurface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (!spDisplay || !spSurface || !LoadWayland())
    {
        return false;
    }
    sFeedbackTypes[0] = wl.surface_interface;
    sFeedbackTypes[1] = &kFeedbackInterface;
    sOutputTypes[0] = wl.output_interface;
    spQueue = wl.display_create_queue(spDisplay);
    void* pDisplay = wl.proxy_create_wrapper(spDisplay);
    wl.proxy_set_queue(pDisplay, spQueue);
    void* pRegistry = wl.proxy_marshal_flags(pDisplay, 1, wl.registry_interface, wl.proxy_get_version(pDisplay), 0, NULL);
    wl.proxy_add_listener(pRegistry, sRegistryListener, NULL);
    wl.display_roundtrip_queue(spDisplay, spQueue);
    if (!spPresentation)
    {
        return false;
    }
    wl.proxy_add_listener(spPresentation, sPresentationListener, NULL);
    wl.display_roundtrip_queue(spDisplay, spQueue);
    return true;
}

// Reads what the socket holds, without waiting, and runs our queue's events.
void PumpWayland()
{
    while (wl.display_prepare_read_queue(spDisplay, spQueue) != 0)
    {
        wl.display_dispatch_queue_pending(spDisplay, spQueue);
    }
    wl.display_flush(spDisplay);
    pollfd p = {wl.display_get_fd(spDisplay), POLLIN, 0};
    if (poll(&p, 1, 0) > 0)
    {
        wl.display_read_events(spDisplay);
    }
    else
    {
        wl.display_cancel_read(spDisplay);
    }
    wl.display_dispatch_queue_pending(spDisplay, spQueue);
}

void RequestFeedback()
{
    void* pFeedback = wl.proxy_marshal_flags(spPresentation, 1, &kFeedbackInterface, 1, 0, spSurface, NULL);
    wl.proxy_add_listener(pFeedback, sFeedbackListener, reinterpret_cast<void*>(static_cast<uintptr_t>(sNextSeq)));
}
#else
bool AttachWayland()
{
    return false;
}

void PumpWayland()
{
}

void RequestFeedback()
{
}
#endif
} // namespace

void port_PresentContent(unsigned long long gameNs)
{
    sNextKind = 'G';
    sNextGameNs = gameNs;
}

namespace PresentLog
{
void Attach(SDL_Window* pWindow)
{
    const char* pPath = getenv("GTA2_PRESENT_LOG");
    if (spFile || !pPath || !*pPath || !(spFile = fopen(pPath, "w")))
    {
        return;
    }
    spWindow = pWindow;
    sFromCompositor = AttachWayland();
    fprintf(spFile, "# video driver %s, display mode refresh %.3f ms, times from %s\n", SDL_GetCurrentVideoDriver(),
            DisplayModeRefreshNs() / 1e6, sFromCompositor ? "the compositor (wp_presentation)" : "the swap");
    AttachGpu();
    fprintf(spFile, "# GPU timer queries: %s\n", sGpu ? "yes" : "unavailable");
    fprintf(spFile, "# kind swap_call_ns swap_return_ns presented_ns msc refresh_ns game_ns flags gpu_begin_ns gpu_end_ns\n");
    sLast10s.mStartNs = NowNs();
    atexit(AtExit);
}

void Repeat()
{
    sNextKind = 'R';
}

void BeforeSwap()
{
    if (!spFile)
    {
        return;
    }
    Entry e = Entry();
    e.mKind = sNextKind;
    e.mGameNs = sNextGameNs;
    e.mSwapCallNs = NowNs();
    e.mGpuSlot = -1;
    sNextKind = 'O';
    if (sGpu && sGpuBeginSlot >= 0)
    {
        gpu.QueryCounter(sGpuQueries[sGpuBeginSlot * 2 + 1], GL_TIMESTAMP);
        e.mGpuSlot = sGpuBeginSlot;
    }
    if (sFromCompositor)
    {
        RequestFeedback();
    }
    sPending.push_back(e);
    sNextSeq++;
}

void AfterSwap()
{
    if (!spFile || sPending.empty())
    {
        return;
    }
    Entry& e = sPending.back();
    e.mSwapReturnNs = NowNs();
    if (sGpu)
    {
        CalibrateGpu(e.mSwapReturnNs);
        sGpuBeginSlot = static_cast<int>(sNextSeq % kGpuSlots);
        gpu.QueryCounter(sGpuQueries[sGpuBeginSlot * 2], GL_TIMESTAMP);
    }
    if (sFromCompositor)
    {
        PumpWayland();
    }
    else
    {
        e.mPresentedNs = e.mSwapReturnNs;
        e.mRefreshNs = DisplayModeRefreshNs();
        e.mDone = true;
    }
    // A compositor that stops answering (hidden window) must not hold the log back forever.
    Flush(256);
}
} // namespace PresentLog
