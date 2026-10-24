// Port (docs/port.md): Windows-style file access on POSIX, see compat/port_files.h.
#if !defined(_WIN32) // non-Windows builds only
#include "compat/port_files.h"
#include <dirent.h>
#include <fnmatch.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <vector>

#undef fopen
#undef _stat

namespace
{
// The name as it is on disk in dir: an exact match first, else one that differs only in case, else name
// itself (a file about to be created). Always scans, so case-insensitive file systems give the same answer.
std::string MatchCase(const std::string& dir, const std::string& name)
{
    DIR* d = opendir(dir.empty() ? "." : dir.c_str());
    std::string found = name;
    bool exact = false;
    if (d)
    {
        while (dirent* e = readdir(d))
        {
            if (strcmp(e->d_name, name.c_str()) == 0)
            {
                exact = true;
                break;
            }
            if (found == name && strcasecmp(e->d_name, name.c_str()) == 0)
            {
                found = e->d_name;
            }
        }
        closedir(d);
    }
    return exact ? name : found;
}

std::string Resolve(const char* path)
{
    std::string in = path ? path : "";
    for (size_t i = 0; i < in.size(); i++)
    {
        in[i] = in[i] == '\\' ? '/' : in[i];
    }
    std::string out = in.size() && in[0] == '/' ? "/" : "";
    size_t start = out.size();
    while (start <= in.size())
    {
        size_t end = in.find('/', start);
        if (end == std::string::npos)
        {
            end = in.size();
        }
        const std::string part = in.substr(start, end - start);
        if (!part.empty() && part != ".")
        {
            const std::string dir = out == "/" ? "/" : out;
            const std::string matched = part == ".." ? part : MatchCase(dir == "/" ? "/" : dir, part);
            out += (out.empty() || out == "/") ? matched : "/" + matched;
        }
        start = end + 1;
    }
    return out.empty() ? "." : out;
}

struct Find
{
    std::vector<std::string> names;
    std::vector<unsigned> sizes;
    std::vector<bool> dirs;
    size_t next;
};

void Fill(Find* f, struct _finddata_t* data)
{
    memset(data, 0, sizeof(*data));
    strncpy(data->name, f->names[f->next].c_str(), sizeof(data->name) - 1);
    data->size = f->sizes[f->next];
    data->attrib = f->dirs[f->next] ? _A_SUBDIR : _A_NORMAL;
    f->next++;
}
} // namespace

extern "C" {

const char* port_resolve_path(const char* path, char* out, size_t outSize)
{
    if (outSize == 0)
    {
        return out;
    }
    const std::string r = Resolve(path);
    strncpy(out, r.c_str(), outSize - 1);
    out[outSize - 1] = 0;
    return out;
}

FILE* port_fopen(const char* path, const char* mode)
{
    return ::fopen(Resolve(path).c_str(), mode);
}

// The pattern's directory is resolved like a path; the file part is matched with fnmatch, ignoring case.
intptr_t port_findfirst(const char* pattern, struct _finddata_t* data)
{
    std::string p = Resolve(pattern);
    const size_t slash = p.rfind('/');
    const std::string dir = slash == std::string::npos ? "." : p.substr(0, slash == 0 ? 1 : slash);
    // Resolve() left the wildcard part as given; match it against the directory's entries
    std::string mask = pattern;
    const size_t back = mask.find_last_of("\\/");
    mask = back == std::string::npos ? mask : mask.substr(back + 1);
    DIR* d = opendir(dir.c_str());
    if (!d)
    {
        return -1;
    }
    Find* f = new Find();
    f->next = 0;
    while (dirent* e = readdir(d))
    {
        if (fnmatch(mask.c_str(), e->d_name, FNM_CASEFOLD) == 0)
        {
            struct stat st;
            const std::string full = dir + "/" + e->d_name;
            const bool ok = ::stat(full.c_str(), &st) == 0;
            f->names.push_back(e->d_name);
            f->sizes.push_back(ok ? (unsigned)st.st_size : 0);
            f->dirs.push_back(ok && S_ISDIR(st.st_mode));
        }
    }
    closedir(d);
    if (f->names.empty())
    {
        delete f;
        return -1;
    }
    Fill(f, data);
    return (intptr_t)f;
}

int port_findnext(intptr_t handle, struct _finddata_t* data)
{
    Find* f = (Find*)handle;
    if (!f || f->next >= f->names.size())
    {
        return -1;
    }
    Fill(f, data);
    return 0;
}

int port_findclose(intptr_t handle)
{
    delete (Find*)handle;
    return 0;
}

int port_stat(const char* path, struct _stat* out)
{
    struct stat st;
    if (::stat(Resolve(path).c_str(), &st) != 0)
    {
        return -1;
    }
    out->st_size = (unsigned int)st.st_size;
    return 0;
}

char* port_getcwd(char* buf, int size)
{
    return getcwd(buf, size);
}

int port_chdir(const char* path)
{
    return chdir(Resolve(path).c_str());
}
}
#endif
