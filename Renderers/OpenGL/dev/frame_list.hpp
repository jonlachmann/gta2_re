// Development aid (README "Development tools"): the frames an environment variable lists, as GTA2GL_CAPTURE,
// GTA2GL_CMDLOG and GTA2GL_TOGGLE_AT give them: <frame>,<first>-<last>,... (Vid_FlipBuffers counts).
#pragma once

#include "../core/gta2_api.hpp"
#include <string>
#include <vector>

class FrameList
{
  public:
    // <envName>=<dir>,<frames>: the frames, and the files for them <dir>/<prefix>_<frame><ext>
    void InitFromEnv(const char* envName, const char* prefix);
    // <list>=<frames>, no directory
    void Parse(const char* list);
    bool Enabled() const
    {
        return !mFrames.empty();
    }
    bool Contains(u32 frame) const;
    std::string PathFor(u32 frame, const char* ext) const;

  private:
    std::string mDir;
    std::string mPrefix;
    std::vector<u32> mFrames;
};
