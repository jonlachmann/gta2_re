// ISO 9660 disc images, with Joliet names when the disc has them.
#include "archive.hpp"

#include <string.h>
#include <set>

namespace setup
{
namespace
{
uint32_t Le32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

class IsoArchive : public Archive
{
public:
    ~IsoArchive() override { fclose(mFile); }

    bool Init(FILE* f, uint32_t sectorSize, uint32_t dataOffset, std::string* pError)
    {
        mFile = f;
        mSectorSize = sectorSize;
        mDataOffset = dataOffset;
        unsigned char root[34] = {};
        bool haveRoot = false;
        for (uint32_t lba = 16; lba < 64; lba++)
        {
            unsigned char vd[2048];
            if (!ReadSector(lba, vd))
            {
                break;
            }
            if (memcmp(vd + 1, "CD001", 5) != 0 || vd[0] == 255)
            {
                break;
            }
            const bool joliet = vd[0] == 2 && vd[88] == '%' && vd[89] == '/' &&
                                (vd[90] == '@' || vd[90] == 'C' || vd[90] == 'E');
            if (vd[0] == 1 && !haveRoot)
            {
                memcpy(root, vd + 156, 34);
                haveRoot = true;
            }
            else if (joliet)
            {
                memcpy(root, vd + 156, 34);
                haveRoot = true;
                mJoliet = true;
            }
        }
        if (!haveRoot)
        {
            *pError = "This disc image has no ISO 9660 file system.";
            return false;
        }
        ReadDir(Le32(root + 2), Le32(root + 10), "", 0);
        return true;
    }

    bool Extract(const std::vector<size_t>& indices, ExtractSink& sink, std::string* pError) override
    {
        std::vector<unsigned char> buf(2048);
        for (size_t index : indices)
        {
            if (!sink.Begin(index))
            {
                return false;
            }
            uint64_t left = mEntries[index].mSize;
            for (uint32_t lba = mExtents[index]; left > 0; lba++)
            {
                if (!ReadSector(lba, buf.data()))
                {
                    *pError = "Cannot read " + mEntries[index].mPath + " from the disc image.";
                    return false;
                }
                const size_t n = left < 2048 ? (size_t)left : 2048;
                if (!sink.Write(buf.data(), n))
                {
                    return false;
                }
                left -= n;
            }
            if (!sink.End())
            {
                return false;
            }
        }
        return true;
    }

private:
    bool ReadSector(uint32_t lba, unsigned char* pOut)
    {
        return ReadAt(mFile, (uint64_t)lba * mSectorSize + mDataOffset, pOut, 2048);
    }

    std::string Name(const unsigned char* p, size_t len)
    {
        std::string name;
        if (mJoliet)
        {
            std::vector<uint16_t> w;
            for (size_t i = 0; i + 1 < len; i += 2)
            {
                w.push_back((uint16_t)((p[i] << 8) | p[i + 1]));
            }
            name = Utf16ToUtf8(w.data(), w.size());
        }
        else
        {
            name.assign(reinterpret_cast<const char*>(p), len);
        }
        const size_t semi = name.find(';');
        if (semi != std::string::npos)
        {
            name.erase(semi);
        }
        if (!name.empty() && name.back() == '.')
        {
            name.pop_back();
        }
        return name;
    }

    void ReadDir(uint32_t lba, uint32_t size, const std::string& prefix, int depth)
    {
        if (depth > 8 || !mVisited.insert(lba).second)
        {
            return;
        }
        std::vector<unsigned char> sector(2048);
        for (uint32_t done = 0; done < size; done += 2048, lba++)
        {
            if (!ReadSector(lba, sector.data()))
            {
                return;
            }
            for (size_t pos = 0; pos < 2048;)
            {
                const unsigned char* r = &sector[pos];
                const unsigned len = r[0];
                if (len < 34 || pos + len > 2048)
                {
                    break;
                }
                pos += len;
                const unsigned nameLen = r[32];
                if (33 + nameLen > len || (nameLen == 1 && r[33] <= 1))
                {
                    continue;
                }
                const std::string path = prefix + Name(r + 33, nameLen);
                if (r[25] & 2)
                {
                    ReadDir(Le32(r + 2), Le32(r + 10), path + "/", depth + 1);
                }
                else
                {
                    mEntries.push_back({path, Le32(r + 10)});
                    mExtents.push_back(Le32(r + 2));
                }
            }
        }
    }

    FILE* mFile = NULL;
    uint32_t mSectorSize = 2048, mDataOffset = 0;
    bool mJoliet = false;
    std::vector<uint32_t> mExtents;
    std::set<uint32_t> mVisited;
};
} // namespace

std::unique_ptr<Archive> OpenIso(FILE* f, uint32_t sectorSize, uint32_t dataOffset, std::string* pError)
{
    std::unique_ptr<IsoArchive> a(new IsoArchive);
    if (!a->Init(f, sectorSize, dataOffset, pError))
    {
        return NULL;
    }
    return a;
}
} // namespace setup
