// The CD's files are installed in data (GTAudio for the music, Movie): a path on a drive letter that
// does not exist is looked up there, so the game's probe of C: to Z: for the music finds it on C:.
static bool IsDrivePath(const char* path)
{
    return ((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}

#if defined(_WIN32)
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <windows.h>

#undef fopen

// Windows paths are the host's: the file system ignores case.
const char* port_resolve_path(const char* path, char* out)
{
    if (IsDrivePath(path) && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
    {
        snprintf(out, PATH_MAX, "data\\%s", path + 3);
    }
    else
    {
        snprintf(out, PATH_MAX, "%s", path);
    }
    return out;
}

FILE* port_fopen(const char* path, const char* mode)
{
    char real[PATH_MAX];
    return fopen(port_resolve_path(path, real), mode);
}

intptr_t port_findfirst(const char* pattern, struct _finddata_t* pData)
{
    const intptr_t handle = _findfirst64i32(pattern, pData);
    if (handle != -1 || !IsDrivePath(pattern))
    {
        return handle;
    }
    char data[PATH_MAX];
    snprintf(data, PATH_MAX, "data\\%s", pattern + 3);
    return _findfirst64i32(data, pData);
}
#else
#include <ctype.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

#undef fopen
#undef remove

// Each lookup is a request to the file system, tens of milliseconds on the 3DS, and the game opens
// the same files again and again: the components found and the directories listed are kept.
static std::map<std::string, std::string>& ResolvedPrefixes() // as asked -> as found
{
    static std::map<std::string, std::string> sResolved;
    return sResolved;
}

static std::map<std::string, std::map<std::string, std::string>>& Listings() // directory -> lower case -> name
{
    static std::map<std::string, std::map<std::string, std::string>> sListings;
    return sListings;
}

static std::string Lower(const std::string& s)
{
    std::string out(s);
    for (char& c : out)
    {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// The name in the directory that matches without regard to case; the listing is read again when it lacks it.
static bool FindInDirectory(const std::string& dir, const std::string& part, std::string* pName)
{
    std::map<std::string, std::string>& names = Listings()[dir];
    const std::string key = Lower(part);
    std::map<std::string, std::string>::const_iterator it = names.find(key);
    if (it == names.end())
    {
        names.clear();
        DIR* d = opendir(dir.empty() ? "." : dir.c_str());
        if (d)
        {
            while (dirent* e = readdir(d))
            {
                names.insert(std::make_pair(Lower(e->d_name), std::string(e->d_name)));
            }
            closedir(d);
        }
        it = names.find(key);
        if (it == names.end())
        {
            return false;
        }
    }
    *pName = it->second;
    return true;
}

// Each component that does not exist as written is looked up in its directory without regard to case.
// A missing last component keeps its spelling, so new files are created as the game names them.
static const char* ResolveHostPath(const char* path, char* out)
{
    std::string p(path);
    for (char& c : p)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    if (p.size() >= 2 && p[1] == ':')
    {
        p.erase(0, 2);
    }

    std::string done = (!p.empty() && p[0] == '/') ? "/" : "";
    std::string asked = done;
    size_t pos = done.size();
    while (pos <= p.size())
    {
        size_t next = p.find('/', pos);
        if (next == std::string::npos)
        {
            next = p.size();
        }
        std::string part = p.substr(pos, next - pos);
        pos = next + 1;
        if (part.empty() || part == ".")
        {
            continue;
        }
        std::string candidate = done + part;
        asked += part;
        const std::map<std::string, std::string>::const_iterator hit = ResolvedPrefixes().find(asked);
        if (hit != ResolvedPrefixes().end())
        {
            candidate = hit->second;
        }
        else if (part != "..")
        {
            std::string name;
            bool found = access(candidate.c_str(), F_OK) == 0;
            if (!found && FindInDirectory(done, part, &name))
            {
                candidate = done + name;
                found = true;
            }
            if (found)
            {
                ResolvedPrefixes()[asked] = candidate;
            }
        }
        done = candidate;
        if (pos <= p.size())
        {
            done += '/';
            asked += '/';
        }
    }
    snprintf(out, PATH_MAX, "%s", done.empty() ? "." : done.c_str());
    return out;
}

const char* port_resolve_path(const char* path, char* out)
{
    ResolveHostPath(path, out);
    if (IsDrivePath(path) && access(out, F_OK) != 0)
    {
        const std::string data = std::string("data\\") + (path + 3);
        ResolveHostPath(data.c_str(), out);
    }
    return out;
}

FILE* port_fopen(const char* path, const char* mode)
{
    char real[PATH_MAX];
    return fopen(port_resolve_path(path, real), mode);
}

int port_remove(const char* path)
{
    char real[PATH_MAX];
    return remove(port_resolve_path(path, real));
}

namespace
{
struct FindState
{
    std::vector<std::string> mNames;
    std::string mDir;
    size_t mNext;
};

// Function-local: global constructors of the game look for files.
std::map<long, FindState>& Finds()
{
    static std::map<long, FindState> sFinds;
    return sFinds;
}
long sNextFind = 1;

void FillFindData(const FindState& state, const std::string& name, _finddata_t* pData)
{
    memset(pData, 0, sizeof(*pData));
    snprintf(pData->name, sizeof(pData->name), "%s", name.c_str());
    struct stat st;
    if (stat((state.mDir + name).c_str(), &st) == 0)
    {
        pData->size = st.st_size;
        pData->time_write = st.st_mtime;
        pData->time_create = st.st_ctime;
        pData->time_access = st.st_atime;
        pData->attrib = S_ISDIR(st.st_mode) ? _A_SUBDIR : _A_NORMAL;
    }
}
} // namespace

long _findfirst(const char* pattern, _finddata_t* pData)
{
    std::string p(pattern);
    for (char& c : p)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    const size_t slash = p.rfind('/');
    std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
    const std::string mask = slash == std::string::npos ? p : p.substr(slash + 1);
    char real[PATH_MAX];
    if (!dir.empty())
    {
        dir = std::string(port_resolve_path(dir.c_str(), real)) + "/";
    }

    FindState state;
    state.mDir = dir;
    state.mNext = 0;
    if (DIR* d = opendir(dir.empty() ? "." : dir.c_str()))
    {
        while (dirent* e = readdir(d))
        {
            if (fnmatch(mask.c_str(), e->d_name, FNM_CASEFOLD) == 0)
            {
                state.mNames.push_back(e->d_name);
            }
        }
        closedir(d);
    }
    if (state.mNames.empty())
    {
        return -1;
    }
    std::sort(state.mNames.begin(), state.mNames.end());
    FillFindData(state, state.mNames[0], pData);
    state.mNext = 1;
    const long handle = sNextFind++;
    Finds()[handle] = state;
    return handle;
}

int _findnext(long handle, _finddata_t* pData)
{
    auto it = Finds().find(handle);
    if (it == Finds().end() || it->second.mNext >= it->second.mNames.size())
    {
        return -1;
    }
    FillFindData(it->second, it->second.mNames[it->second.mNext++], pData);
    return 0;
}

int _findclose(long handle)
{
    return Finds().erase(handle) ? 0 : -1;
}

int _stat(const char* path, struct _stat* pStat)
{
    char real[PATH_MAX];
    struct stat st;
    if (stat(port_resolve_path(path, real), &st) != 0)
    {
        return -1;
    }
    pStat->st_mode = st.st_mode;
    pStat->st_size = st.st_size;
    return 0;
}

int _chdir(const char* path)
{
    char real[PATH_MAX];
    return chdir(port_resolve_path(path, real));
}

char* _getcwd(char* buf, int size)
{
    return getcwd(buf, size);
}

int _strnicmp(const char* a, const char* b, size_t n)
{
    return strncasecmp(a, b, n);
}

int _stricmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

char* _strupr(char* s)
{
    for (char* p = s; *p; p++)
    {
        *p = (char)toupper((unsigned char)*p);
    }
    return s;
}

char* _itoa(int value, char* dst, int radix)
{
    if (radix == 10)
    {
        sprintf(dst, "%d", value);
        return dst;
    }
    char buf[40];
    unsigned v = (unsigned)value;
    int i = 0;
    do
    {
        unsigned d = v % radix;
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= radix;
    } while (v);
    int o = 0;
    while (i)
    {
        dst[o++] = buf[--i];
    }
    dst[o] = 0;
    return dst;
}
#endif
