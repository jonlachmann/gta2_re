// Copying the game's data from an install folder, an archive or an installer into the game folder.
#pragma once

#include "fs.hpp"

#include <atomic>
#include <mutex>
#include <vector>

namespace setup
{
struct DataCheck
{
    std::vector<std::string> mMissing; // essential files, relative to the game folder
    int mMusicFound = 0;
    int mMusicTotal = 0;
};
DataCheck CheckGameData(const fs::path& root);

struct Progress
{
    std::atomic<uint64_t> mDone{0}, mTotal{0}; // bytes, or files when the sizes are not known
    std::atomic<bool> mCountsFiles{false};
    std::atomic<bool> mCancel{false};
    std::mutex mMutex;
    std::string mStatus; // under mMutex

    void SetStatus(const std::string& s)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStatus = s;
    }
    std::string Status()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mStatus;
    }
};

struct InstallResult
{
    bool mOk = false;
    bool mCancelled = false;
    bool mMusicOnly = false; // the source had only the music, added to the data already there
    std::string mError;
    DataCheck mCheck;
};

// source: a folder or a file (archive, disc image, installer). dest: the game folder, which gets
// data/, player/ and test/.
InstallResult Install(const fs::path& source, const fs::path& dest, Progress& progress);
} // namespace setup
