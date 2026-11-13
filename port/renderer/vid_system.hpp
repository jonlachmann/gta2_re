// Portable side of the Vid_* API: modes, the CPU surface the game writes to, frame flow and capture.
#pragma once

#include "capture.hpp"
#include "options.hpp"
#include "gl_renderer.hpp"
#include "renderer.hpp"
#include <vector>

class IGLContext;

class VidSystem
{
  public:
    // Pixel value the CPU surface is filled with; untouched pixels are not composited.
    static const u16 kSurfaceKey = 0x0821;
    static const s32 kWindowedModeId = -2;
    static const size_t kMaxModes = 64;

    VidSystem();
    ~VidSystem();

    static VidSystem* FromVideo(SVideo* pVideo);
    SVideo* Video() { return &mHeader.mVideo; }
    // Headless (no GL context ever): gbh_InitDLL then finds no renderer, and gbh_* draws nothing
    void DrawNothing() { mHeader.mRenderer = 0; }

    // iniPath: gta2gl.ini next to the DLLs.
    void Init(void* hInstance, u16 flags, void* pVidFuncs, const char* iniPath);
    // Live switch between the faithful renderer and the ini options.
    void ToggleFaithful();
    void SetConfiguredOptions(const RenderOptions& options);

    s32 CheckMode(s32 width, s32 height, s32 bpp);
    SDisplayMode* FindMode(s32 modeId);
    s32 FindFirstMode(s32 bppFilter);
    s32 FindNextMode();
    SDevice* FindDevice(s32 deviceId);

    // Context creation is left to the caller (platform); width/height are the logical size.
    bool AttachContext(IGLContext* pContext, const char** ppError);
    bool HasContext() const { return mContext != 0; }
    void SetMode(s32 modeId, s32 width, s32 height);

    void GetSurface();
    void FreeSurface();
    // scanSurface false: nothing wrote the CPU surface since GetSurface, so it is not composited.
    void Flip(bool scanSurface = true);
    // Shows the last flipped frame again, for one more refresh.
    void Represent();
    void Clear(u8 r, u8 g, u8 b, s32 left, s32 top, s32 right, s32 bottom);
    s32 SetGamma(f32 r, f32 g, f32 b);

    GLRenderer& Renderer() { return mRenderer; }
    const u32* FrameCounter() const { return &mFrame; }
    // GTA2GL_CAPTURE frames are these test frames (port_FrameCount), or flips with GTA2GL_CAPTURE_FLIPS=1.
    void SetTestFrame(u32 frame) { mTestFrame = frame; }

  private:
    void ResizeSurface(s32 width, s32 height);

    GLVideoHeader mHeader; // must stay first
    GLRenderer mRenderer;
    IGLContext* mContext;
    std::vector<SDisplayMode> mModes;
    s32 mEnumIdx;
    s32 mEnumBpp;
    SDevice mDevice;
    std::vector<u16> mSurface;
    s32 mSurfaceW;
    s32 mSurfaceH;
    bool mSurfaceGrabbed;
    bool mSurfaceClean; // holds only kSurfaceKey
    u32 mFrame;
    FrameCapture mCapture;
    bool mCaptureFlips;
    u32 mTestFrame;
    u32 mCapturedFrame;
    OptionsFile mOptions;
    std::vector<u32> mToggleFrames; // GTA2GL_TOGGLE_AT=<frame>,...: scripted ToggleFaithful for tests
    void ApplyOptions();

    // GTA2GL_STATS=<frames>: logs frame rate, draw calls, uploads and present time every <frames> flips.
    u32 mStatsInterval;
    double mStatsStart;
    double mPresentSeconds;
    double mWaitSeconds;
    u32 mStatsDraws;
    u32 mStatsUploads;
    u32 mStatsVerts;
};
