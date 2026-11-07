// Development aids in the video side's frame flow (README "Development tools"), all off unless their variable is set:
// frames written as BMP (GTA2GL_CAPTURE, capture_now), the F12 switch pressed at given frames (GTA2GL_TOGGLE_AT) and
// the frame statistics (GTA2GL_STATS=<n>: frame rate, draw calls, uploads and present time every n frames).
#pragma once

#include "capture.hpp"
#include "frame_list.hpp"

class GLRenderer;

class FrameTools
{
  public:
    void Init();
    // VidSystem::Flip, in order: before the frame is finished; once it is (writes it when wanted); around the wait
    // for the GPU; at the end (the statistics)
    void BeginFrame();
    void Capture(GLRenderer& renderer, u32 frame);
    void BeginWait();
    void EndWait();
    void EndFrame(const GLRenderer& renderer, u32 frame);
    bool ToggleAt(u32 frame) const
    {
        return mToggleFrames.Contains(frame);
    }

  private:
    FrameCapture mCapture;
    FrameList mToggleFrames;

    u32 mStatsInterval = 0;
    double mFrameStart = 0;
    double mWaitStart = 0;
    double mStatsStart = 0;
    double mPresentSeconds = 0; // post effects + present + swap
    double mWaitSeconds = 0;
    u32 mStatsDraws = 0;
    u32 mStatsUploads = 0;
    u32 mStatsVerts = 0;
};
