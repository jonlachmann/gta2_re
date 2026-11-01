#if defined(_WIN32)
#include <limits.h>
#include <stdio.h>

// Windows paths are the host's: the file system ignores case.
const char* port_resolve_path(const char* path, char* out)
{
    snprintf(out, PATH_MAX, "%s", path);
    return out;
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

// Each component that does not exist as written is looked up in its directory without regard to case.
// A missing last component keeps its spelling, so new files are created as the game names them.
const char* port_resolve_path(const char* path, char* out)
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
        if (part != ".." && access(candidate.c_str(), F_OK) != 0)
        {
            DIR* d = opendir(done.empty() ? "." : done.c_str());
            if (d)
            {
                while (dirent* e = readdir(d))
                {
                    if (strcasecmp(e->d_name, part.c_str()) == 0)
                    {
                        candidate = done + e->d_name;
                        break;
                    }
                }
                closedir(d);
            }
        }
        done = candidate;
        if (pos <= p.size())
        {
            done += '/';
        }
    }
    snprintf(out, PATH_MAX, "%s", done.empty() ? "." : done.c_str());
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
