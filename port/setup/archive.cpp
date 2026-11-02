#include "archive.hpp"

#include "miniz.h"

#include <string.h>

namespace setup
{
std::string Utf16ToUtf8(const uint16_t* p, size_t count)
{
    std::string out;
    for (size_t i = 0; i < count; i++)
    {
        uint32_t c = p[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < count && p[i + 1] >= 0xDC00 && p[i + 1] < 0xE000)
        {
            c = 0x10000 + ((c - 0xD800) << 10) + (p[++i] - 0xDC00);
        }
        if (c < 0x80)
        {
            out += (char)c;
        }
        else if (c < 0x800)
        {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += (char)(0xE0 | (c >> 12));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (c >> 18));
            out += (char)(0x80 | ((c >> 12) & 0x3F));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

namespace
{
class ZipArchive : public Archive
{
public:
    ~ZipArchive() override
    {
        mz_zip_reader_end(&mZip);
        fclose(mFile);
    }

    bool Init(FILE* f, std::string* pError)
    {
        mFile = f;
        memset(&mZip, 0, sizeof(mZip));
        // miniz takes the archive to start at the current position.
        if (!Seek64(f, 0) || !mz_zip_reader_init_cfile(&mZip, f, 0, 0))
        {
            *pError = "This is not a readable zip archive.";
            return false;
        }
        const mz_uint count = mz_zip_reader_get_num_files(&mZip);
        for (mz_uint i = 0; i < count; i++)
        {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&mZip, i, &st) || st.m_is_directory)
            {
                continue;
            }
            mEntries.push_back({st.m_filename, st.m_uncomp_size});
            mIndices.push_back(i);
        }
        return true;
    }

    bool Extract(const std::vector<size_t>& indices, ExtractSink& sink, std::string* pError) override
    {
        for (size_t index : indices)
        {
            if (!sink.Begin(index))
            {
                return false;
            }
            if (!mz_zip_reader_extract_to_callback(&mZip, mIndices[index], WriteCallback, &sink, 0))
            {
                if (pError->empty())
                {
                    *pError = std::string("Cannot extract ") + mEntries[index].mPath + ": " +
                              mz_zip_get_error_string(mz_zip_get_last_error(&mZip));
                }
                return false;
            }
            if (!sink.End())
            {
                return false;
            }
        }
        return true;
    }

private:
    static size_t WriteCallback(void* pOpaque, mz_uint64, const void* pBuf, size_t n)
    {
        return static_cast<ExtractSink*>(pOpaque)->Write(pBuf, n) ? n : 0;
    }

    FILE* mFile = NULL;
    mz_zip_archive mZip;
    std::vector<mz_uint> mIndices;
};

bool Contains(const std::vector<unsigned char>& buf, const char* s)
{
    const size_t n = strlen(s);
    for (size_t i = 0; i + n <= buf.size(); i++)
    {
        if (memcmp(&buf[i], s, n) == 0)
        {
            return true;
        }
    }
    return false;
}
} // namespace

std::unique_ptr<Archive> OpenZip(FILE* f, std::string* pError)
{
    std::unique_ptr<ZipArchive> zip(new ZipArchive);
    if (!zip->Init(f, pError))
    {
        return NULL;
    }
    return zip;
}

std::unique_ptr<Archive> OpenArchive(const fs::path& path, std::string* pError)
{
    FILE* f = OpenFile(path, "rb");
    if (!f)
    {
        *pError = "Cannot open the file.";
        return NULL;
    }
    const uint64_t size = FileSize64(f);
    unsigned char head[64] = {};
    ReadAt(f, 0, head, sizeof(head));

    if (memcmp(head, "7z\xBC\xAF\x27\x1C", 6) == 0)
    {
        fclose(f);
        return Open7z(path, pError);
    }
    if (memcmp(head, "PK\x03\x04", 4) == 0 || memcmp(head, "PK\x05\x06", 4) == 0)
    {
        return OpenZip(f, pError);
    }
    // ISO 9660 volume descriptor at sector 16: plain 2048-byte sectors, or raw 2352-byte ones
    // (.bin) in mode 1 or mode 2.
    static const struct
    {
        uint32_t mSectorSize, mDataOffset;
    } kLayouts[] = {{2048, 0}, {2352, 16}, {2352, 24}};
    for (const auto& layout : kLayouts)
    {
        char id[5];
        if (ReadAt(f, 16ull * layout.mSectorSize + layout.mDataOffset + 1, id, 5) && memcmp(id, "CD001", 5) == 0)
        {
            return OpenIso(f, layout.mSectorSize, layout.mDataOffset, pError);
        }
    }
    if (memcmp(head, "ISc(", 4) == 0)
    {
        fclose(f);
        *pError = "This is an InstallShield cabinet (data1.cab or data1.hdr). The port cannot read "
                  "InstallShield cabinets yet: install the game with its installer, then choose "
                  "\"Use an existing install\".";
        return NULL;
    }
    if (head[0] == 'M' && head[1] == 'Z')
    {
        uint64_t nsisPos;
        if (FindNsisHeader(f, &nsisPos))
        {
            return OpenNsis(f, nsisPos, pError);
        }
        std::vector<unsigned char> buf((size_t)(size < (4u << 20) ? size : (4u << 20)));
        ReadAt(f, 0, buf.data(), buf.size());
        // A self-extracting zip.
        if (std::unique_ptr<Archive> zip = OpenZip(f, pError))
        {
            return zip;
        }
        if (Contains(buf, "Inno Setup"))
        {
            *pError = "This is an Inno Setup installer, which the port cannot read yet. Install the "
                      "game with it (or extract it with innoextract), then choose \"Use an existing "
                      "install\".";
        }
        else if (Contains(buf, "InstallShield"))
        {
            *pError = "This is an InstallShield installer (as used by the original CD and the 2004 "
                      "Rockstar Classics download), which the port cannot read yet. Install the game "
                      "with it, then choose \"Use an existing install\".";
        }
        else
        {
            *pError = "This program is not an installer the port can read. Supported: NSIS "
                      "installers, .zip, .7z and .iso files.";
        }
        return NULL;
    }
    if (std::unique_ptr<Archive> zip = OpenZip(f, pError))
    {
        return zip;
    }
    *pError = "Unknown file type. Supported: NSIS installers, .zip, .7z and .iso files.";
    return NULL;
}
} // namespace setup
