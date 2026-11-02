#include "fs.hpp"

#include <ctype.h>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace setup
{
fs::path PathFromUtf8(const std::string& s)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string PathToUtf8(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string Lower(std::string s)
{
    for (char& c : s)
    {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

bool FindNoCase(const fs::path& root, const std::string& rel, fs::path* pFound)
{
    fs::path cur = root;
    size_t pos = 0;
    while (pos <= rel.size())
    {
        size_t next = rel.find('/', pos);
        if (next == std::string::npos)
        {
            next = rel.size();
        }
        const std::string part = rel.substr(pos, next - pos);
        pos = next + 1;
        if (part.empty())
        {
            continue;
        }
        std::error_code ec;
        fs::path candidate = cur / PathFromUtf8(part);
        if (!fs::exists(candidate, ec))
        {
            bool found = false;
            const std::string want = Lower(part);
            for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec))
            {
                if (Lower(PathToUtf8(it->path().filename())) == want)
                {
                    candidate = it->path();
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
        }
        cur = candidate;
    }
    *pFound = cur;
    return true;
}

FILE* OpenFile(const fs::path& p, const char* mode)
{
#if defined(_WIN32)
    wchar_t wmode[8] = {};
    for (int i = 0; mode[i] && i < 7; i++)
    {
        wmode[i] = (wchar_t)mode[i];
    }
    return _wfopen(p.c_str(), wmode);
#else
    return fopen(p.c_str(), mode);
#endif
}

bool Seek64(FILE* f, uint64_t pos)
{
#if defined(_WIN32)
    return _fseeki64(f, (long long)pos, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)pos, SEEK_SET) == 0;
#endif
}

uint64_t FileSize64(FILE* f)
{
#if defined(_WIN32)
    _fseeki64(f, 0, SEEK_END);
    return (uint64_t)_ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    return (uint64_t)ftello(f);
#endif
}

bool ReadAt(FILE* f, uint64_t pos, void* dst, size_t size)
{
    return Seek64(f, pos) && fread(dst, 1, size, f) == size;
}

bool SetWorkingDir(const fs::path& p)
{
#if defined(_WIN32)
    return SetCurrentDirectoryW(p.c_str()) != 0;
#else
    return chdir(p.c_str()) == 0;
#endif
}
} // namespace setup
