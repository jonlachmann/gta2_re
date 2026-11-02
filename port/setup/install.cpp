#include "install.hpp"

#include "archive.hpp"

#include <string.h>
#include <map>
#include <system_error>

namespace setup
{
namespace
{
// Enough to reach the menus and play the three cities; the rest of data/ is copied as found.
const char* const kEssential[] = {
    "data/fstyle.sty", "data/wil.sty", "data/ste.sty", "data/bil.sty",
    "data/wil.gmp", "data/ste.gmp", "data/bil.gmp",
    "data/wil.scr", "data/ste.scr", "data/bil.scr",
    "data/e.gxt", "data/nyc.gci", "data/keyboard/eng_kb.cfg",
    "data/audio/fstyle.sdt", "data/audio/fstyle.raw", "data/audio/wil.sdt", "data/audio/wil.raw",
    "data/audio/ste.sdt", "data/audio/ste.raw", "data/audio/bil.sdt", "data/audio/bil.raw",
    "data/frontend/1.tga", "data/frontend/2.tga", "data/frontend/3.tga", "data/frontend/mask.tga",
};

// The game's music and radio, read from the CD (X:\GTAUDIO) by the original; data/GTAudio here.
const char* const kMusic[] = {
    "1", "10", "101a", "10a", "11", "11a", "12", "2", "3", "4", "5",
    "5a", "6", "6a", "7", "7a", "8", "8a", "9", "9a", "a", "d",
};
const char kMusicDir[] = "data/GTAudio/";

// Folder picked by mistake (a home folder): stop listing.
const size_t kMaxFolderFiles = 20000;

class FolderArchive : public Archive
{
public:
    bool Init(const fs::path& root, std::string* pError)
    {
        mRoot = root;
        // The music folder itself.
        const std::string prefix = Lower(PathToUtf8(root.filename())) == "gtaudio" ? "GTAudio/" : "";
        mPrefixSize = prefix.size();
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            if (it.depth() > 5)
            {
                it.disable_recursion_pending();
            }
            std::error_code ec2;
            if (!it->is_regular_file(ec2))
            {
                continue;
            }
            if (mEntries.size() >= kMaxFolderFiles)
            {
                *pError = "This folder holds too many files. Choose the GTA2 folder itself (the one that "
                          "contains data).";
                return false;
            }
            const std::u8string rel = it->path().lexically_relative(root).generic_u8string();
            mEntries.push_back({prefix + std::string(reinterpret_cast<const char*>(rel.data()), rel.size()),
                                (uint64_t)it->file_size(ec2)});
        }
        return true;
    }

    bool Extract(const std::vector<size_t>& indices, ExtractSink& sink, std::string* pError) override
    {
        std::vector<unsigned char> buf(1 << 18);
        for (size_t index : indices)
        {
            FILE* f = OpenFile(mRoot / PathFromUtf8(mEntries[index].mPath.substr(mPrefixSize)), "rb");
            if (!f)
            {
                *pError = "Cannot read " + mEntries[index].mPath;
                return false;
            }
            bool ok = sink.Begin(index);
            while (ok)
            {
                const size_t n = fread(buf.data(), 1, buf.size(), f);
                if (n == 0)
                {
                    break;
                }
                ok = sink.Write(buf.data(), n);
            }
            fclose(f);
            if (!ok || !sink.End())
            {
                return false;
            }
        }
        return true;
    }

private:
    fs::path mRoot;
    size_t mPrefixSize = 0;
};

struct Planned
{
    size_t mIndex;
    std::string mDest; // relative to the game folder
};

bool EndsWith(const std::string& s, const char* suffix)
{
    const size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string BaseName(const std::string& p)
{
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

bool IsMusicFile(const std::string& lowerPath)
{
    const size_t slash = lowerPath.rfind('/');
    const bool inMusicDir = slash != std::string::npos && slash >= 7 &&
                            lowerPath.compare(slash - 7, 7, "gtaudio") == 0 &&
                            (slash == 7 || lowerPath[slash - 8] == '/');
    return inMusicDir && EndsWith(lowerPath, ".wav");
}

// What goes where: the source's data folder is the one holding fstyle.sty, the music any folder
// named GTAudio. *pRoot: the folder that holds data, relative to the source.
std::vector<Planned> Plan(const Archive& a, bool* pHasData, std::string* pRoot)
{
    const std::vector<ArchiveEntry>& entries = a.Entries();
    std::string prefix;
    bool found = false;
    for (const ArchiveEntry& e : entries)
    {
        const std::string lower = Lower(e.mPath);
        if (lower == "fstyle.sty" && !found)
        {
            prefix.clear();
            found = true;
        }
        if ((lower == "data/fstyle.sty" || EndsWith(lower, "/data/fstyle.sty")) &&
            (!found || lower.size() - 10 < prefix.size()))
        {
            prefix = e.mPath.substr(0, e.mPath.size() - 10);
            found = true;
        }
    }
    *pHasData = found;
    *pRoot = prefix.size() >= 5 ? prefix.substr(0, prefix.size() - 5) : "..";

    std::vector<Planned> plan;
    std::map<std::string, bool> taken;
    const std::string lowerPrefix = Lower(prefix);
    for (size_t i = 0; i < entries.size(); i++)
    {
        const std::string lower = Lower(entries[i].mPath);
        std::string dest;
        if (found && lower.compare(0, lowerPrefix.size(), lowerPrefix) == 0)
        {
            dest = "data/" + entries[i].mPath.substr(prefix.size());
            if (Lower(dest).compare(0, strlen(kMusicDir), Lower(kMusicDir)) == 0)
            {
                dest = kMusicDir + dest.substr(strlen(kMusicDir));
            }
        }
        else if (IsMusicFile(lower))
        {
            dest = kMusicDir + BaseName(entries[i].mPath);
        }
        if (dest.empty() || dest.find("..") != std::string::npos || taken.count(Lower(dest)))
        {
            continue;
        }
        taken[Lower(dest)] = true;
        plan.push_back({i, dest});
    }
    return plan;
}

class InstallSink : public ExtractSink
{
public:
    InstallSink(const std::map<size_t, std::string>& dests, const fs::path& staging, Progress& progress,
                bool byCount)
        : mDests(dests), mStaging(staging), mProgress(progress), mByCount(byCount)
    {
    }
    ~InstallSink() override
    {
        if (mFile)
        {
            fclose(mFile);
        }
    }

    bool Begin(size_t index) override
    {
        if (mProgress.mCancel)
        {
            return false;
        }
        const std::string& dest = mDests.at(index);
        mProgress.SetStatus(dest);
        const fs::path p = mStaging / PathFromUtf8(dest);
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        mFile = OpenFile(p, "wb");
        if (!mFile)
        {
            mError = "Cannot write " + PathToUtf8(p);
        }
        return mFile != NULL;
    }

    bool Write(const void* pData, size_t size) override
    {
        if (mProgress.mCancel)
        {
            return false;
        }
        if (fwrite(pData, 1, size, mFile) != size)
        {
            mError = "Cannot write the game data (is the disk full?).";
            return false;
        }
        if (!mByCount)
        {
            mProgress.mDone += size;
        }
        return true;
    }

    bool End() override
    {
        const bool ok = fclose(mFile) == 0;
        mFile = NULL;
        if (mByCount)
        {
            mProgress.mDone += 1;
        }
        if (!ok)
        {
            mError = "Cannot write the game data (is the disk full?).";
        }
        return ok;
    }

    std::string mError;

private:
    const std::map<size_t, std::string>& mDests;
    fs::path mStaging;
    Progress& mProgress;
    bool mByCount;
    FILE* mFile = NULL;
};

void RemoveAll(const fs::path& p)
{
    std::error_code ec;
    fs::remove_all(p, ec);
}

bool MoveInto(const fs::path& from, const fs::path& to, std::string* pError)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if (ec)
    {
        *pError = "Cannot move " + PathToUtf8(from) + " to " + PathToUtf8(to) + ": " + ec.message();
        return false;
    }
    return true;
}
} // namespace

DataCheck CheckGameData(const fs::path& root)
{
    DataCheck check;
    fs::path found;
    for (const char* rel : kEssential)
    {
        if (!FindNoCase(root, rel, &found))
        {
            check.mMissing.push_back(rel);
        }
    }
    for (const char* name : kMusic)
    {
        check.mMusicTotal++;
        if (FindNoCase(root, std::string(kMusicDir) + name + ".wav", &found))
        {
            check.mMusicFound++;
        }
    }
    return check;
}

InstallResult Install(const fs::path& source, const fs::path& dest, Progress& progress)
{
    InstallResult result;
    std::error_code ec;
    const bool isFolder = fs::is_directory(source, ec);
    progress.SetStatus(isFolder ? "Looking at the folder..." : "Opening the file...");

    std::unique_ptr<Archive> archive;
    if (isFolder)
    {
        std::unique_ptr<FolderArchive> folder(new FolderArchive);
        if (folder->Init(source, &result.mError))
        {
            archive = std::move(folder);
        }
    }
    else
    {
        archive = OpenArchive(source, &result.mError);
    }
    if (!archive)
    {
        return result;
    }

    bool hasData;
    std::string rootRel;
    const std::vector<Planned> plan = Plan(*archive, &hasData, &rootRel);
    const bool haveDataAlready = CheckGameData(dest).mMissing.empty();
    if (!hasData)
    {
        bool cabinets = false;
        for (const ArchiveEntry& e : archive->Entries())
        {
            const std::string name = Lower(BaseName(e.mPath));
            cabinets |= name == "data1.cab" || name == "data1.hdr";
        }
        if (plan.empty() || !haveDataAlready)
        {
            result.mError = cabinets ? "The game files here are packed in InstallShield cabinets (data1.cab), "
                                       "which the port cannot read yet. Install the game with its installer, "
                                       "then choose \"Use an existing install\"."
                                     : "No GTA2 data found here (looked for data/fstyle.sty).";
            if (!plan.empty())
            {
                result.mError += " The music found here can be added once the game data is installed.";
            }
            return result;
        }
        result.mMusicOnly = true;
    }

    uint64_t total = 0;
    std::map<size_t, std::string> dests;
    std::vector<size_t> indices;
    for (const Planned& p : plan)
    {
        total += archive->Entries()[p.mIndex].mSize;
        dests[p.mIndex] = p.mDest;
        indices.push_back(p.mIndex);
    }
    const bool byCount = total == 0;
    const fs::space_info space = fs::space(dest, ec);
    if (!ec && total + (16u << 20) > space.available)
    {
        result.mError = "Not enough free disk space: " + std::to_string((total >> 20) + 16) + " MB needed in " +
                        PathToUtf8(dest) + ".";
        return result;
    }

    const fs::path staging = dest / "setup.tmp";
    RemoveAll(staging);
    fs::create_directories(staging, ec);
    progress.mCountsFiles = byCount;
    progress.mTotal = byCount ? indices.size() : total;
    progress.mDone = 0;
    InstallSink sink(dests, staging, progress, byCount);
    const bool extracted = archive->Extract(indices, sink, &result.mError);
    if (!extracted || progress.mCancel)
    {
        result.mCancelled = progress.mCancel;
        if (result.mError.empty())
        {
            result.mError = sink.mError.empty() ? "Cancelled." : sink.mError;
        }
        RemoveAll(staging);
        return result;
    }

    progress.SetStatus("Checking the files...");
    fs::path found;
    if (result.mMusicOnly)
    {
        fs::path musicDir = dest / PathFromUtf8(kMusicDir);
        if (FindNoCase(dest, kMusicDir, &found))
        {
            musicDir = found;
        }
        fs::create_directories(musicDir, ec);
        for (const Planned& p : plan)
        {
            const std::string name = BaseName(p.mDest);
            fs::path target = musicDir / PathFromUtf8(name);
            if (FindNoCase(musicDir, name, &found))
            {
                fs::remove(found, ec);
            }
            if (!MoveInto(staging / PathFromUtf8(p.mDest), target, &result.mError))
            {
                RemoveAll(staging);
                return result;
            }
        }
    }
    else
    {
        result.mCheck = CheckGameData(staging);
        if (!result.mCheck.mMissing.empty())
        {
            result.mError = "The game data found here is incomplete. Missing:";
            for (size_t i = 0; i < result.mCheck.mMissing.size() && i < 8; i++)
            {
                result.mError += "\n  " + result.mCheck.mMissing[i];
            }
            RemoveAll(staging);
            return result;
        }
        const fs::path old = dest / "data.old";
        RemoveAll(old);
        if (FindNoCase(dest, "data", &found))
        {
            fs::rename(found, old, ec);
        }
        if (!MoveInto(staging / "data", dest / "data", &result.mError))
        {
            RemoveAll(staging);
            return result;
        }
        RemoveAll(old);

        // A folder the port already ran from: its settings and saves come along.
        if (isFolder && !haveDataAlready)
        {
            const fs::path root = (source / PathFromUtf8(rootRel)).lexically_normal();
            if (FindNoCase(root, "gta2_registry.ini", &found))
            {
                fs::copy_file(found, dest / "gta2_registry.ini", fs::copy_options::overwrite_existing, ec);
            }
            if (FindNoCase(root, "gta2gl.ini", &found))
            {
                fs::copy_file(found, dest / "gta2gl.ini", fs::copy_options::skip_existing, ec);
            }
            if (FindNoCase(root, "player", &found) && fs::is_directory(found, ec))
            {
                fs::create_directories(dest / "player", ec);
                for (fs::directory_iterator it(found, ec), end; !ec && it != end; it.increment(ec))
                {
                    std::error_code ec2;
                    if (it->is_regular_file(ec2))
                    {
                        fs::copy_file(it->path(), dest / "player" / it->path().filename(), fs::copy_options::skip_existing,
                                      ec2);
                    }
                }
            }
        }
    }
    RemoveAll(staging);
    fs::create_directories(dest / "player", ec);
    fs::create_directories(dest / "test", ec);
    result.mCheck = CheckGameData(dest);
    result.mOk = true;
    return result;
}
} // namespace setup
