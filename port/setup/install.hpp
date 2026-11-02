// Copying the game's data into the game folder (the working directory) from an install folder,
// a disc, an archive or an installer.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace setup
{
namespace fs = std::filesystem;

fs::path PathFromUtf8(const std::string& s);
std::string PathToUtf8(const fs::path& p);
FILE* OpenFile(const fs::path& p, const char* mode);
// rel uses '/'; each component that does not exist as written is matched without regard to case.
bool FindNoCase(const fs::path& root, const std::string& rel, fs::path* pFound);

struct DataCheck
{
    std::vector<std::string> mMissing; // essential files
    int mMusicFound = 0;
    int mMusicTotal = 0;
};
DataCheck CheckGameData(const fs::path& root);

struct Progress
{
    std::atomic<uint64_t> mDone{0}, mTotal{0}; // bytes
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
    std::string mError;
    DataCheck mCheck;
};

// source: a folder, or a file (InstallShield setup, disc image, archive).
InstallResult Install(const fs::path& source, Progress& progress);

// The largest image of gta2.exe's icon (group 0x65), as a BMP file.
bool SaveExeIcon(const fs::path& exe, const fs::path& bmp);
} // namespace setup
