// Development aid (README "Development tools"): frames written as BMP, the ones GTA2GL_CAPTURE=<dir>,<frames> lists,
// or on demand: a file capture_now in the working directory (checked every 30 frames) has the next frame written to
// capture.bmp, and is removed.
#pragma once

#include "frame_list.hpp"

class FrameCapture
{
  public:
    // Reads GTA2GL_CAPTURE once. prefix names the files: <dir>/<prefix>_<frame>.bmp
    void Init(const char* prefix)
    {
        mFrames.InitFromEnv("GTA2GL_CAPTURE", prefix);
    }
    bool WantFrame(u32 frame) const
    {
        return mFrames.Contains(frame);
    }
    static bool WantNow(u32 frame); // capture_now is there (removed): this frame to capture.bmp
    std::string PathFor(u32 frame) const
    {
        return mFrames.PathFor(frame, ".bmp");
    }

    // rgba: width*height pixels, top row first.
    static bool WriteBmp(const std::string& path, const u8* rgba, s32 width, s32 height, bool bottomUp);

  private:
    FrameList mFrames;
};
