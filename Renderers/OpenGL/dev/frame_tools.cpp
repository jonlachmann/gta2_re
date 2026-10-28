#include "frame_tools.hpp"
#include "../core/gl_renderer.hpp"
#include <chrono>
#include <stdio.h>
#include <stdlib.h>

namespace
{
double Now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

void FrameTools::Init()
{
    mCapture.Init("gl");
    const char* pStats = getenv("GTA2GL_STATS");
    mStatsInterval = pStats ? (u32)atoi(pStats) : 0;
    mToggleFrames.Parse(getenv("GTA2GL_TOGGLE_AT"));
}

void FrameTools::BeginFrame()
{
    mFrameStart = mStatsInterval ? Now() : 0.0;
}

void FrameTools::Capture(GLRenderer& renderer, u32 frame)
{
    const bool listed = mCapture.WantFrame(frame), now = FrameCapture::WantNow(frame);
    if (listed || now)
    {
        std::vector<u8> rgba;
        renderer.ReadLogical(rgba);
        FrameCapture::WriteBmp(listed ? mCapture.PathFor(frame) : std::string("capture.bmp"),
                               &rgba[0],
                               renderer.LogicalWidth(),
                               renderer.LogicalHeight(),
                               false);
    }
}

void FrameTools::BeginWait()
{
    mWaitStart = mStatsInterval ? Now() : 0.0;
}

void FrameTools::EndWait()
{
    if (mStatsInterval)
    {
        const double wait = Now() - mWaitStart;
        mWaitSeconds += wait;
        mPresentSeconds -= wait; // EndFrame adds the whole frame
    }
}

void FrameTools::EndFrame(const GLRenderer& renderer, u32 frame)
{
    if (!mStatsInterval)
    {
        return;
    }
    const double t1 = Now();
    mPresentSeconds += t1 - mFrameStart;
    if (frame % mStatsInterval != 0)
    {
        return;
    }
    if (mStatsStart > 0.0)
    {
        const double n = mStatsInterval;
        fprintf(stderr,
                "glstats frame %u: %.1f fps, %.1f draw calls, %.0f vertices, %.1f uploads, post+present+swap %.3f ms, gpu wait "
                "%.3f ms per frame\n",
                frame,
                n / (t1 - mStatsStart),
                (renderer.GetDrawCallCount() - mStatsDraws) / n,
                (renderer.GetVertexCount() - mStatsVerts) / n,
                (renderer.GetUploadCount() - mStatsUploads) / n,
                mPresentSeconds * 1000.0 / n,
                mWaitSeconds * 1000.0 / n);
    }
    mStatsStart = t1;
    mPresentSeconds = 0.0;
    mWaitSeconds = 0.0;
    mStatsDraws = renderer.GetDrawCallCount();
    mStatsUploads = renderer.GetUploadCount();
    mStatsVerts = renderer.GetVertexCount();
}
