#include "install.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <ctype.h>
#include <libunshield.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <memory>

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
// Constant data only: the setup runs before the global constructors.
const char kStaging[] = "setup.tmp";

std::string Lower(std::string s)
{
    for (char& c : s)
    {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

bool EndsWith(const std::string& s, const std::string& end)
{
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

std::string Generic(const fs::path& p)
{
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

bool Seek(FILE* f, uint64_t pos)
{
#if defined(_WIN32)
    return _fseeki64(f, (long long)pos, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)pos, SEEK_SET) == 0;
#endif
}

uint64_t Tell(FILE* f)
{
#if defined(_WIN32)
    return (uint64_t)_ftelli64(f);
#else
    return (uint64_t)ftello(f);
#endif
}

// Writes what read gives (0 at the end, < 0 on an error) to the file, counted in the progress.
template <class Read> bool WriteFile(const fs::path& to, Read read, Progress& progress, std::string* pError)
{
    progress.SetStatus(PathToUtf8(to.filename()));
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    FILE* f = OpenFile(to, "wb");
    if (!f)
    {
        *pError = "Cannot write " + PathToUtf8(to);
        return false;
    }
    std::vector<char> buf(1 << 18);
    bool ok = true;
    while (ok && !progress.mCancel)
    {
        const long n = read(buf.data(), buf.size());
        if (n <= 0)
        {
            ok = n == 0;
            *pError = ok ? "" : "Cannot read " + PathToUtf8(to.filename()) + " from the source.";
            break;
        }
        ok = fwrite(buf.data(), 1, n, f) == (size_t)n;
        progress.mDone += n;
    }
    ok = fclose(f) == 0 && ok && !progress.mCancel;
    if (!ok && pError->empty())
    {
        *pError = progress.mCancel ? "Cancelled." : "Cannot write the game data (is the disk full?).";
    }
    return ok;
}

struct Entry
{
    std::string mPath; // '/' separated
    uint64_t mSize;
};

// The files of a folder, archive or installer; Extract writes the wanted ones (index -> file).
class Source
{
public:
    virtual ~Source() {}
    virtual bool Extract(const std::map<size_t, fs::path>& wanted, Progress& progress, std::string* pError) = 0;
    std::vector<Entry> mEntries;
};

class FolderSource : public Source
{
public:
    explicit FolderSource(const fs::path& root) : mRoot(root)
    {
        // The music folder itself.
        const std::string prefix = Lower(PathToUtf8(root.filename())) == "gtaudio" ? "GTAudio/" : "";
        mPrefix = prefix.size();
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && mEntries.size() < 20000; it.increment(ec))
        {
            std::error_code ec2;
            if (it.depth() > 4)
            {
                it.disable_recursion_pending();
            }
            if (it->is_regular_file(ec2))
            {
                mEntries.push_back({prefix + Generic(it->path().lexically_relative(root)), (uint64_t)it->file_size(ec2)});
            }
        }
    }

    bool Extract(const std::map<size_t, fs::path>& wanted, Progress& progress, std::string* pError) override
    {
        for (const auto& w : wanted)
        {
            FILE* in = OpenFile(mRoot / PathFromUtf8(mEntries[w.first].mPath.substr(mPrefix)), "rb");
            const bool ok = in && WriteFile(w.second, [&](char* p, size_t n) { return (long)fread(p, 1, n, in); },
                                            progress, pError);
            if (in)
            {
                fclose(in);
            }
            if (!ok)
            {
                *pError = in ? *pError : "Cannot read " + mEntries[w.first].mPath;
                return false;
            }
        }
        return true;
    }

private:
    fs::path mRoot;
    size_t mPrefix = 0;
};

// InstallShield's self-extracting setup.exe: after the PE image, each file as its name, path,
// version and size (strings) then its bytes.
class SetupExeSource : public Source
{
public:
    ~SetupExeSource() override { fclose(mFile); }

    static std::unique_ptr<Source> Open(const fs::path& path)
    {
        FILE* f = OpenFile(path, "rb");
        unsigned char h[1024];
        if (!f || fread(h, 1, sizeof(h), f) != sizeof(h) || h[0] != 'M' || h[1] != 'Z')
        {
            if (f)
            {
                fclose(f);
            }
            return NULL;
        }
        std::unique_ptr<SetupExeSource> s(new SetupExeSource);
        s->mFile = f;
        const uint32_t pe = Le32(h + 0x3C);
        uint64_t pos = 0;
        if (pe + 24 <= sizeof(h) && memcmp(h + pe, "PE\0\0", 4) == 0)
        {
            const unsigned sections = h[pe + 6] | (h[pe + 7] << 8), optional = h[pe + 20] | (h[pe + 21] << 8);
            for (unsigned i = 0; i < sections && pe + 24 + optional + 40 * (i + 1) <= sizeof(h); i++)
            {
                const unsigned char* sec = h + pe + 24 + optional + 40 * i;
                pos = std::max<uint64_t>(pos, (uint64_t)Le32(sec + 16) + Le32(sec + 20));
            }
        }
        std::string fields[4];
        while (pos && Seek(f, pos) && s->ReadFields(fields))
        {
            const uint64_t size = strtoull(fields[3].c_str(), NULL, 10);
            s->mEntries.push_back({fields[0], size});
            s->mOffsets.push_back(Tell(f));
            pos = s->mOffsets.back() + size;
        }
        if (s->mEntries.empty())
        {
            return NULL;
        }
        return s;
    }

    bool Extract(const std::map<size_t, fs::path>& wanted, Progress& progress, std::string* pError) override
    {
        for (const auto& w : wanted)
        {
            uint64_t left = mEntries[w.first].mSize;
            if (!Seek(mFile, mOffsets[w.first]) ||
                !WriteFile(w.second,
                           [&](char* p, size_t n) {
                               const size_t got = fread(p, 1, (size_t)std::min<uint64_t>(n, left), mFile);
                               left -= got;
                               return got == 0 && left > 0 ? -1L : (long)got;
                           },
                           progress, pError))
            {
                return false;
            }
        }
        return true;
    }

private:
    static uint32_t Le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

    bool ReadFields(std::string* pFields)
    {
        for (int i = 0; i < 4; i++)
        {
            pFields[i].clear();
            int c;
            while ((c = fgetc(mFile)) > 0 && isprint(c) && pFields[i].size() < 512)
            {
                pFields[i] += (char)c;
            }
            if (c != 0 || pFields[i].empty())
            {
                return false;
            }
        }
        return isdigit((unsigned char)pFields[3][0]) != 0;
    }

    FILE* mFile = NULL;
    std::vector<uint64_t> mOffsets;
};

// Disc images and archives, through libarchive.
class ArchiveSource : public Source
{
public:
    static std::unique_ptr<Source> Open(const fs::path& path)
    {
        std::unique_ptr<ArchiveSource> s(new ArchiveSource);
        s->mPath = path;
        archive* a = s->Begin();
        archive_entry* e;
        int r = ARCHIVE_FATAL;
        while (a && (r = archive_read_next_header(a, &e)) == ARCHIVE_OK)
        {
            if (archive_entry_filetype(e) == AE_IFREG)
            {
                const char* name = archive_entry_pathname_utf8(e);
                s->mEntries.push_back({name ? name : archive_entry_pathname(e), (uint64_t)archive_entry_size(e)});
            }
        }
        archive_read_free(a);
        return r == ARCHIVE_EOF && !s->mEntries.empty() ? std::move(s) : NULL;
    }

    bool Extract(const std::map<size_t, fs::path>& wanted, Progress& progress, std::string* pError) override
    {
        archive* a = Begin();
        archive_entry* e;
        size_t index = 0;
        bool ok = a != NULL;
        while (ok && archive_read_next_header(a, &e) == ARCHIVE_OK)
        {
            if (archive_entry_filetype(e) != AE_IFREG)
            {
                continue;
            }
            const auto it = wanted.find(index++);
            if (it != wanted.end())
            {
                ok = WriteFile(it->second, [&](char* p, size_t n) { return (long)archive_read_data(a, p, n); },
                               progress, pError);
            }
        }
        archive_read_free(a);
        return ok;
    }

private:
    archive* Begin()
    {
        archive* a = archive_read_new();
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);
#if defined(_WIN32)
        const int r = archive_read_open_filename_w(a, mPath.c_str(), 1 << 16);
#else
        const int r = archive_read_open_filename(a, mPath.c_str(), 1 << 16);
#endif
        if (r != ARCHIVE_OK)
        {
            archive_read_free(a);
            return NULL;
        }
        return a;
    }

    fs::path mPath;
};

// InstallShield cabinets (data1.hdr and data*.cab), through unshield.
class CabinetSource : public Source
{
public:
    ~CabinetSource() override { unshield_close(mUnshield); }

    static std::unique_ptr<Source> Open(const fs::path& hdr)
    {
        unshield_set_log_level(0);
        Unshield* u = unshield_open(PathToUtf8(hdr).c_str());
        if (!u)
        {
            return NULL;
        }
        std::unique_ptr<CabinetSource> s(new CabinetSource);
        s->mUnshield = u;
        for (int i = 0; i < unshield_file_count(u); i++)
        {
            if (unshield_file_is_valid(u, i))
            {
                std::string path = unshield_directory_name(u, unshield_file_directory(u, i));
                path += (path.empty() ? "" : "/") + std::string(unshield_file_name(u, i));
                for (char& c : path)
                {
                    c = c == '\\' ? '/' : c;
                }
                s->mEntries.push_back({path, unshield_file_size(u, i)});
                s->mIndices.push_back(i);
            }
        }
        return s;
    }

    bool Extract(const std::map<size_t, fs::path>& wanted, Progress& progress, std::string* pError) override
    {
        for (const auto& w : wanted)
        {
            progress.SetStatus(PathToUtf8(w.second.filename()));
            std::error_code ec;
            fs::create_directories(w.second.parent_path(), ec);
            if (progress.mCancel || !unshield_file_save(mUnshield, mIndices[w.first], PathToUtf8(w.second).c_str()))
            {
                *pError = progress.mCancel ? "Cancelled." : "Cannot unpack " + mEntries[w.first].mPath;
                return false;
            }
            progress.mDone += mEntries[w.first].mSize;
        }
        return true;
    }

private:
    Unshield* mUnshield = NULL;
    std::vector<int> mIndices;
};

std::unique_ptr<Source> OpenSource(const fs::path& path)
{
    std::error_code ec;
    if (fs::is_directory(path, ec))
    {
        return std::unique_ptr<Source>(new FolderSource(path));
    }
    std::unique_ptr<Source> s = SetupExeSource::Open(path);
    return s ? std::move(s) : ArchiveSource::Open(path);
}

// What goes where. The source's data folder is the one holding fstyle.sty, the music any folder
// named GTAudio.
struct Plan
{
    std::map<size_t, fs::path> mFiles; // into kStaging
    bool mHasData = false;
    std::string mRoot; // the folder that holds data, relative to the source
    uint64_t mBytes = 0;
};

Plan MakePlan(const std::vector<Entry>& entries)
{
    Plan plan;
    std::string prefix;
    for (const Entry& e : entries)
    {
        const std::string lower = Lower(e.mPath);
        if ((lower == "fstyle.sty" || lower == "data/fstyle.sty" || EndsWith(lower, "/data/fstyle.sty")) &&
            (!plan.mHasData || lower.size() - 10 < prefix.size()))
        {
            prefix = e.mPath.substr(0, e.mPath.size() - 10);
            plan.mHasData = true;
        }
    }
    plan.mRoot = prefix.size() >= 5 ? prefix.substr(0, prefix.size() - 5) : "..";
    const std::string lowerPrefix = Lower(prefix);
    std::map<std::string, bool> taken;
    for (size_t i = 0; i < entries.size(); i++)
    {
        const std::string lower = Lower(entries[i].mPath);
        const size_t slash = lower.rfind('/');
        std::string dest;
        if (plan.mHasData && lower.compare(0, lowerPrefix.size(), lowerPrefix) == 0)
        {
            dest = "data/" + entries[i].mPath.substr(prefix.size());
        }
        else if (slash != std::string::npos && EndsWith(lower.substr(0, slash), "gtaudio") && EndsWith(lower, ".wav"))
        {
            dest = std::string(kMusicDir) + entries[i].mPath.substr(slash + 1);
        }
        if (Lower(dest).compare(0, strlen(kMusicDir), Lower(kMusicDir)) == 0)
        {
            dest = kMusicDir + dest.substr(strlen(kMusicDir));
        }
        if (dest.empty() || dest.find("..") != std::string::npos || taken[Lower(dest)])
        {
            continue;
        }
        taken[Lower(dest)] = true;
        plan.mFiles[i] = fs::path(kStaging) / PathFromUtf8(dest);
        plan.mBytes += entries[i].mSize;
    }
    return plan;
}

bool EnoughSpace(uint64_t bytes, std::string* pError)
{
    std::error_code ec;
    const fs::space_info space = fs::space(".", ec);
    if (!ec && bytes + (16u << 20) > space.available)
    {
        *pError = "Not enough free disk space: " + std::to_string((bytes >> 20) + 16) + " MB needed.";
        return false;
    }
    return true;
}

InstallResult Fail(const std::string& error, const Progress& progress)
{
    std::error_code ec;
    fs::remove_all(kStaging, ec);
    InstallResult result;
    result.mCancelled = progress.mCancel;
    result.mError = error.empty() ? "Cancelled." : error;
    return result;
}

// The InstallShield cabinets beside data1.hdr, copied out of the source so that unshield can open them.
std::unique_ptr<Source> ExtractCabinets(Source& source, Progress& progress, std::string* pError)
{
    std::string dir;
    bool found = false;
    for (const Entry& e : source.mEntries)
    {
        if (Lower(e.mPath) == "data1.hdr" || EndsWith(Lower(e.mPath), "/data1.hdr"))
        {
            dir = Lower(e.mPath.substr(0, e.mPath.size() - 9));
            found = true;
        }
    }
    std::map<size_t, fs::path> cabinets;
    uint64_t bytes = 0;
    for (size_t i = 0; found && i < source.mEntries.size(); i++)
    {
        const std::string lower = Lower(source.mEntries[i].mPath);
        const std::string name = lower.substr(dir.size());
        if (lower.compare(0, dir.size(), dir) == 0 && name.compare(0, 4, "data") == 0 &&
            (EndsWith(name, ".cab") || EndsWith(name, ".hdr")) && name.find('/') == std::string::npos)
        {
            cabinets[i] = fs::path(kStaging) / "cab" / name;
            bytes += source.mEntries[i].mSize;
        }
    }
    if (cabinets.empty() || !EnoughSpace(bytes * 2, pError))
    {
        return NULL;
    }
    progress.mTotal = bytes;
    if (!source.Extract(cabinets, progress, pError))
    {
        return NULL;
    }
    progress.mDone = 0;
    std::unique_ptr<Source> cab = CabinetSource::Open(fs::path(kStaging) / "cab" / "data1.hdr");
    if (!cab)
    {
        *pError = "These InstallShield cabinets cannot be read.";
    }
    return cab;
}

bool MoveInto(const fs::path& from, const fs::path& to, std::string* pError)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    *pError = ec ? "Cannot move " + PathToUtf8(from) + " to " + PathToUtf8(to) + ": " + ec.message() : "";
    return !ec;
}

} // namespace

InstallResult Install(const fs::path& sourcePath, Progress& progress)
{
    std::error_code ec;
    fs::remove_all(kStaging, ec);
    progress.SetStatus("Reading the source...");
    std::unique_ptr<Source> source = OpenSource(sourcePath);
    if (!source)
    {
        return Fail("This file is not an installer, disc image or archive the port can read.", progress);
    }
    Plan plan = MakePlan(source->mEntries);
    if (!plan.mHasData)
    {
        std::string error;
        if (std::unique_ptr<Source> cab = ExtractCabinets(*source, progress, &error))
        {
            source = std::move(cab);
            plan = MakePlan(source->mEntries);
        }
        else if (!error.empty())
        {
            return Fail(error, progress);
        }
    }
    const bool haveData = CheckGameData(".").mMissing.empty();
    const bool musicOnly = !plan.mHasData && !plan.mFiles.empty() && haveData;
    if (!plan.mHasData && !musicOnly)
    {
        return Fail("No GTA2 data found here (looked for data/fstyle.sty, or InstallShield's data1.hdr).", progress);
    }
    std::string error;
    if (!EnoughSpace(plan.mBytes, &error))
    {
        return Fail(error, progress);
    }
    progress.mTotal = plan.mBytes;
    if (!source->Extract(plan.mFiles, progress, &error))
    {
        return Fail(error, progress);
    }
    source.reset();
    progress.SetStatus("Checking the files...");

    fs::path found;
    if (musicOnly)
    {
        fs::path musicDir = kMusicDir;
        if (FindNoCase(".", kMusicDir, &found))
        {
            musicDir = found;
        }
        for (const auto& f : plan.mFiles)
        {
            if (FindNoCase(musicDir, PathToUtf8(f.second.filename()), &found))
            {
                fs::remove(found, ec);
            }
            if (!MoveInto(f.second, musicDir / f.second.filename(), &error))
            {
                return Fail(error, progress);
            }
        }
    }
    else
    {
        const DataCheck check = CheckGameData(kStaging);
        if (!check.mMissing.empty())
        {
            std::string missing;
            for (size_t i = 0; i < check.mMissing.size() && i < 8; i++)
            {
                missing += "\n  " + check.mMissing[i];
            }
            return Fail("The game data found here is incomplete. Missing:" + missing, progress);
        }
        fs::remove_all("data.old", ec);
        if (FindNoCase(".", "data", &found))
        {
            fs::rename(found, "data.old", ec);
        }
        if (!MoveInto(fs::path(kStaging) / "data", "data", &error))
        {
            return Fail(error, progress);
        }
        fs::remove_all("data.old", ec);

        // A folder the port already ran from: its settings and saves come along.
        const fs::path root = (sourcePath / PathFromUtf8(plan.mRoot)).lexically_normal();
        if (fs::is_directory(sourcePath, ec) && !haveData)
        {
            for (const char* name : {"gta2_registry.ini", "gta2gl.ini"})
            {
                if (FindNoCase(root, name, &found))
                {
                    fs::copy_file(found, name, fs::copy_options::overwrite_existing, ec);
                }
            }
            if (FindNoCase(root, "player", &found))
            {
                fs::create_directories("player", ec);
                fs::copy(found, "player", fs::copy_options::skip_existing | fs::copy_options::recursive, ec);
            }
        }
    }
    fs::remove_all(kStaging, ec);
    InstallResult result;
    result.mCheck = CheckGameData(".");
    result.mOk = true;
    return result;
}

fs::path PathFromUtf8(const std::string& s)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string PathToUtf8(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

FILE* OpenFile(const fs::path& p, const char* mode)
{
#if defined(_WIN32)
    const std::wstring wmode(mode, mode + strlen(mode));
    return _wfopen(p.c_str(), wmode.c_str());
#else
    return fopen(p.c_str(), mode);
#endif
}

bool FindNoCase(const fs::path& root, const std::string& rel, fs::path* pFound)
{
    fs::path cur = root;
    for (size_t pos = 0; pos < rel.size();)
    {
        const size_t next = std::min(rel.find('/', pos), rel.size());
        const std::string part = rel.substr(pos, next - pos);
        pos = next + 1;
        std::error_code ec;
        fs::path candidate = cur / PathFromUtf8(part);
        if (!fs::exists(candidate, ec))
        {
            candidate.clear();
            for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec))
            {
                if (Lower(PathToUtf8(it->path().filename())) == Lower(part))
                {
                    candidate = it->path();
                    break;
                }
            }
            if (candidate.empty())
            {
                return false;
            }
        }
        cur = candidate;
    }
    *pFound = cur;
    return true;
}

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
        check.mMusicFound += FindNoCase(root, std::string(kMusicDir) + name + ".wav", &found);
    }
    return check;
}
} // namespace setup
