// NSIS installers (2.x and 3.x, LZMA or deflate, solid or not), read without running them: the
// files come from the script's File commands, under the folders its SetOutPath commands name.
// Only files under $INSTDIR are listed, and their sizes are not known before extraction.
#include "archive.hpp"

extern "C" {
#include "LzmaDec.h"
}
#include "miniz.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>

namespace setup
{
namespace
{
const char kInstDir[] = "<INSTDIR>";

enum
{
    kOpCreateDir = 11, // SetOutPath when its second argument is set
    kOpExtractFile = 20,
    kOpAssignVar = 25, // StrCpy, as File /r uses for $_OUTDIR
};

enum Method
{
    kLzma,
    kDeflate,
    kBzip2,
};

uint32_t Le32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t Le16(const unsigned char* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool IsLzmaProps(const unsigned char* p)
{
    const uint32_t dict = Le32(p + 1);
    return p[0] == 0x5D && dict >= (1u << 12) && dict <= (1u << 30);
}

void* AllocFn(ISzAllocPtr, size_t size)
{
    return malloc(size);
}
void FreeFn(ISzAllocPtr, void* p)
{
    free(p);
}
const ISzAlloc kAlloc = {AllocFn, FreeFn};

// A compressed byte range of the file, decompressed as it is read.
class Decoder
{
public:
    Decoder(FILE* f, uint64_t pos, uint64_t end) : mFile(f), mPos(pos), mEnd(end), mIn(1 << 16) {}
    virtual ~Decoder() {}
    // Up to size bytes; 0 at the end of the stream or on an error.
    virtual size_t ReadSome(void* pDst, size_t size) = 0;

    bool Read(void* pDst, size_t size)
    {
        unsigned char* p = static_cast<unsigned char*>(pDst);
        while (size > 0)
        {
            const size_t n = ReadSome(p, size);
            if (n == 0)
            {
                return false;
            }
            p += n;
            size -= n;
        }
        return true;
    }

    bool Skip(uint64_t size)
    {
        unsigned char buf[1 << 14];
        while (size > 0)
        {
            const size_t n = ReadSome(buf, size < sizeof(buf) ? (size_t)size : sizeof(buf));
            if (n == 0)
            {
                return false;
            }
            size -= n;
        }
        return true;
    }

protected:
    bool Refill()
    {
        if (mInPos < mInSize || mPos >= mEnd)
        {
            return mInPos < mInSize;
        }
        const uint64_t left = mEnd - mPos;
        mInSize = left < mIn.size() ? (size_t)left : mIn.size();
        mInPos = 0;
        if (!ReadAt(mFile, mPos, mIn.data(), mInSize))
        {
            mInSize = 0;
            return false;
        }
        mPos += mInSize;
        return true;
    }

    FILE* mFile;
    uint64_t mPos, mEnd;
    std::vector<unsigned char> mIn;
    size_t mInPos = 0, mInSize = 0;
};

class LzmaDecoder : public Decoder
{
public:
    LzmaDecoder(FILE* f, uint64_t pos, uint64_t end) : Decoder(f, pos, end) { LzmaDec_Construct(&mDec); }
    ~LzmaDecoder() override { LzmaDec_Free(&mDec, &kAlloc); }

    bool Init()
    {
        unsigned char props[LZMA_PROPS_SIZE];
        for (unsigned char& b : props)
        {
            if (!Refill())
            {
                return false;
            }
            b = mIn[mInPos++];
        }
        if (LzmaDec_Allocate(&mDec, props, LZMA_PROPS_SIZE, &kAlloc) != SZ_OK)
        {
            return false;
        }
        LzmaDec_Init(&mDec);
        return true;
    }

    size_t ReadSome(void* pDst, size_t size) override
    {
        for (;;)
        {
            Refill();
            SizeT outLen = size;
            SizeT inLen = mInSize - mInPos;
            ELzmaStatus status;
            if (LzmaDec_DecodeToBuf(&mDec, static_cast<Byte*>(pDst), &outLen, mIn.data() + mInPos, &inLen, LZMA_FINISH_ANY,
                                    &status) != SZ_OK)
            {
                return 0;
            }
            mInPos += inLen;
            if (outLen > 0)
            {
                return outLen;
            }
            if (status == LZMA_STATUS_FINISHED_WITH_MARK || (inLen == 0 && mInPos == mInSize && mPos >= mEnd))
            {
                return 0;
            }
        }
    }

private:
    CLzmaDec mDec;
};

class InflateDecoder : public Decoder
{
public:
    InflateDecoder(FILE* f, uint64_t pos, uint64_t end) : Decoder(f, pos, end)
    {
        memset(&mZ, 0, sizeof(mZ));
        mOk = mz_inflateInit2(&mZ, -MZ_DEFAULT_WINDOW_BITS) == MZ_OK;
    }
    ~InflateDecoder() override { mz_inflateEnd(&mZ); }

    size_t ReadSome(void* pDst, size_t size) override
    {
        while (mOk && !mDone)
        {
            Refill();
            mZ.next_in = mIn.data() + mInPos;
            mZ.avail_in = (unsigned)(mInSize - mInPos);
            mZ.next_out = static_cast<unsigned char*>(pDst);
            mZ.avail_out = (unsigned)size;
            const int ret = mz_inflate(&mZ, MZ_NO_FLUSH);
            const size_t used = (mInSize - mInPos) - mZ.avail_in;
            const size_t out = size - mZ.avail_out;
            mInPos += used;
            if (ret == MZ_STREAM_END)
            {
                mDone = true;
            }
            else if (ret != MZ_OK && ret != MZ_BUF_ERROR)
            {
                mOk = false;
            }
            if (out > 0)
            {
                return out;
            }
            if (used == 0 && mInPos == mInSize && mPos >= mEnd)
            {
                return 0;
            }
        }
        return 0;
    }

private:
    mz_stream mZ;
    bool mOk = false, mDone = false;
};

// An uncompressed block.
class CopyDecoder : public Decoder
{
public:
    using Decoder::Decoder;
    size_t ReadSome(void* pDst, size_t size) override
    {
        if (!Refill())
        {
            return 0;
        }
        const size_t n = std::min(size, mInSize - mInPos);
        memcpy(pDst, mIn.data() + mInPos, n);
        mInPos += n;
        return n;
    }
};

struct NsisFile
{
    uint32_t mDataPos;
};

class NsisArchive : public Archive
{
public:
    ~NsisArchive() override { fclose(mFile); }

    bool Init(FILE* f, uint64_t headerPos, std::string* pError)
    {
        mFile = f;
        mFileSize = FileSize64(f);
        unsigned char fh[28];
        if (!ReadAt(f, headerPos, fh, sizeof(fh)))
        {
            *pError = "Damaged NSIS installer.";
            return false;
        }
        const uint32_t headerSize = Le32(fh + 20);
        mDataStart = headerPos + sizeof(fh);
        unsigned char sig[16] = {};
        ReadAt(f, mDataStart, sig, sizeof(sig));

        // As 7-Zip tells them apart: LZMA has its properties first, a non-solid installer a block
        // length with the compressed bit, bzip2 its block size digit.
        auto lzmaAt = [&](const unsigned char* p, bool* pFlag) {
            if (IsLzmaProps(p))
            {
                *pFlag = false;
                return true;
            }
            if (p[0] <= 1 && IsLzmaProps(p + 1))
            {
                *pFlag = true;
                return true;
            }
            return false;
        };
        if (lzmaAt(sig, &mFilterFlag))
        {
            mMethod = kLzma;
            mSolid = true;
        }
        else if (lzmaAt(sig + 4, &mFilterFlag))
        {
            mMethod = kLzma;
            mSolid = false;
        }
        else if (sig[3] == 0x80)
        {
            mMethod = (sig[4] == 0x31 && sig[5] < 14) ? kBzip2 : kDeflate;
            mSolid = false;
        }
        else
        {
            mMethod = (sig[0] == 0x31 && sig[1] < 14) ? kBzip2 : kDeflate;
            mSolid = true;
        }
        if (mMethod == kBzip2)
        {
            *pError = "This NSIS installer uses bzip2 compression, which the port cannot read yet.";
            return false;
        }
        if (mFilterFlag && sig[mSolid ? 0 : 4] == 1)
        {
            *pError = "This NSIS installer uses the x86 filter, which the port cannot read yet.";
            return false;
        }

        std::vector<unsigned char> header(headerSize);
        if (mSolid)
        {
            std::unique_ptr<Decoder> d = MakeDecoder(mDataStart, mFileSize, true);
            unsigned char len[4];
            if (!d || !d->Read(len, 4) || (Le32(len) & 0x7FFFFFFF) != headerSize || !d->Read(header.data(), headerSize))
            {
                *pError = "Damaged NSIS installer (cannot unpack its header).";
                return false;
            }
        }
        else
        {
            unsigned char len[4];
            ReadAt(f, mDataStart, len, 4);
            const uint32_t blockLen = Le32(len) & 0x7FFFFFFF;
            std::unique_ptr<Decoder> d = MakeDecoder(mDataStart + 4, mDataStart + 4 + blockLen, (Le32(len) >> 31) != 0);
            if (!d || !d->Read(header.data(), headerSize))
            {
                *pError = "Damaged NSIS installer (cannot unpack its header).";
                return false;
            }
            mDataArea = mDataStart + 4 + blockLen;
        }
        if (!ParseHeader(header))
        {
            *pError = "This NSIS installer's script could not be read.";
            return false;
        }
        return true;
    }

    bool Extract(const std::vector<size_t>& indices, ExtractSink& sink, std::string* pError) override
    {
        // Identical files share their data: each data position is read once.
        std::map<uint32_t, std::vector<size_t>> byPos;
        for (size_t index : indices)
        {
            byPos[mFiles[index].mDataPos].push_back(index);
        }
        std::unique_ptr<Decoder> solid;
        uint64_t solidPos = 0;
        if (mSolid)
        {
            solid = MakeDecoder(mDataStart, mFileSize, true);
            if (!solid || !solid->Skip(4 + (uint64_t)mHeaderSize))
            {
                *pError = "Damaged NSIS installer.";
                return false;
            }
        }
        std::vector<unsigned char> buf;
        for (auto& group : byPos)
        {
            std::unique_ptr<Decoder> block;
            Decoder* d;
            uint64_t size = 0;
            bool sized = true;
            unsigned char len[4];
            if (mSolid)
            {
                if (group.first < solidPos || !solid->Skip(group.first - solidPos) || !solid->Read(len, 4))
                {
                    *pError = "Damaged NSIS installer.";
                    return false;
                }
                size = Le32(len) & 0x7FFFFFFF;
                solidPos = group.first + 4 + size;
                d = solid.get();
            }
            else
            {
                const uint64_t at = mDataArea + group.first;
                if (!ReadAt(mFile, at, len, 4))
                {
                    *pError = "Damaged NSIS installer.";
                    return false;
                }
                const uint32_t blockLen = Le32(len) & 0x7FFFFFFF;
                const bool compressed = (Le32(len) >> 31) != 0;
                block = MakeDecoder(at + 4, at + 4 + blockLen, compressed);
                if (!block)
                {
                    *pError = "Damaged NSIS installer.";
                    return false;
                }
                size = blockLen;
                sized = !compressed;
                d = block.get();
            }
            buf.clear();
            unsigned char chunk[1 << 16];
            for (uint64_t left = size; !sized || left > 0;)
            {
                const size_t want = sized ? (size_t)std::min<uint64_t>(left, sizeof(chunk)) : sizeof(chunk);
                const size_t n = d->ReadSome(chunk, want);
                if (n == 0)
                {
                    if (sized)
                    {
                        *pError = "Damaged NSIS installer (a file is cut short).";
                        return false;
                    }
                    break;
                }
                buf.insert(buf.end(), chunk, chunk + n);
                left -= sized ? n : 0;
            }
            for (size_t index : group.second)
            {
                if (!sink.Begin(index) || !sink.Write(buf.data(), buf.size()) || !sink.End())
                {
                    return false;
                }
            }
        }
        return true;
    }

private:
    std::unique_ptr<Decoder> MakeDecoder(uint64_t pos, uint64_t end, bool compressed)
    {
        if (!compressed)
        {
            return std::unique_ptr<Decoder>(new CopyDecoder(mFile, pos, end));
        }
        if (mMethod == kDeflate)
        {
            return std::unique_ptr<Decoder>(new InflateDecoder(mFile, pos, end));
        }
        std::unique_ptr<LzmaDecoder> d(new LzmaDecoder(mFile, pos + (mFilterFlag ? 1 : 0), end));
        if (!d->Init())
        {
            return NULL;
        }
        return d;
    }

    // A string of the script, with $INSTDIR as kInstDir, $OUTDIR as the current output folder and
    // other variables, shell folders and language strings as <...> markers.
    std::string String(uint32_t index)
    {
        std::string out;
        if (mUnicode)
        {
            std::vector<uint16_t> run;
            auto flush = [&]() {
                out += Utf16ToUtf8(run.data(), run.size());
                run.clear();
            };
            for (uint64_t pos = mStrings + 2ull * index; pos + 1 < mHeader.size(); pos += 2)
            {
                const uint16_t c = Le16(&mHeader[pos]);
                if (c == 0)
                {
                    break;
                }
                if (c > 4 || pos + 3 >= mHeader.size())
                {
                    run.push_back(c);
                    continue;
                }
                pos += 2;
                uint16_t n = Le16(&mHeader[pos]);
                if (c == 4)
                {
                    run.push_back(n);
                    continue;
                }
                flush();
                if (c == 3)
                {
                    AppendVar(out, (n & 0x7F) | (((n >> 8) & 0x7F) << 7));
                }
                else
                {
                    out += "<?>";
                }
            }
            flush();
            return out;
        }
        const unsigned codeSkip = mNsis3 ? 4 : 252, codeVar = mNsis3 ? 3 : 253;
        for (uint64_t pos = mStrings + index; pos < mHeader.size(); pos++)
        {
            const unsigned c = mHeader[pos];
            if (c == 0)
            {
                break;
            }
            const bool code = mNsis3 ? c <= 4 : c >= 252;
            if (!code || pos + 2 >= mHeader.size())
            {
                out += (char)c;
                continue;
            }
            if (c == codeSkip)
            {
                out += (char)mHeader[++pos];
                continue;
            }
            const unsigned c0 = mHeader[pos + 1], c1 = mHeader[pos + 2];
            pos += 2;
            if (c == codeVar)
            {
                AppendVar(out, (c0 & 0x7F) | ((c1 & 0x7F) << 7));
            }
            else
            {
                out += "<?>";
            }
        }
        return out;
    }

    void AppendVar(std::string& out, unsigned n)
    {
        if (n == 21)
        {
            out += kInstDir;
        }
        else if (n == 22)
        {
            out += mOutDir;
        }
        else
        {
            const auto it = mVars.find(n);
            out += it != mVars.end() ? it->second : "<?>";
        }
    }

    bool ParseHeader(std::vector<unsigned char>& header)
    {
        mHeader.swap(header);
        mHeaderSize = (uint32_t)mHeader.size();
        if (mHeader.size() < 4 + 8 * 8)
        {
            return false;
        }
        const uint32_t entries = Le32(&mHeader[4 + 2 * 8]), numEntries = Le32(&mHeader[4 + 2 * 8 + 4]);
        mStrings = Le32(&mHeader[4 + 3 * 8]);
        if (entries > mHeader.size() || numEntries > (mHeader.size() - entries) / 28 || mStrings + 2 > mHeader.size())
        {
            return false;
        }
        mUnicode = Le16(&mHeader[mStrings]) == 0;
        // NSIS 3 codes a variable as 3 then two bytes with the top bit set.
        mNsis3 = false;
        if (mUnicode)
        {
            for (size_t p = mStrings; p + 6 <= mHeader.size(); p += 2)
            {
                if (Le16(&mHeader[p]) == 0 && Le16(&mHeader[p + 2]) == 3 && (Le16(&mHeader[p + 4]) & 0x8080) == 0x8080)
                {
                    mNsis3 = true;
                    break;
                }
            }
        }
        else
        {
            for (size_t p = mStrings; p + 3 <= mHeader.size(); p++)
            {
                if (mHeader[p] == 0 && mHeader[p + 1] == 3 && (mHeader[p + 2] & 0x80))
                {
                    mNsis3 = true;
                    break;
                }
            }
        }
        if (mUnicode && !mNsis3)
        {
            return false; // the "Park" Unicode builds of NSIS 2
        }

        std::map<std::string, size_t> seen;
        for (uint32_t i = 0; i < numEntries; i++)
        {
            const unsigned char* e = &mHeader[entries + i * 28];
            const uint32_t which = Le32(e);
            if (which == kOpCreateDir && Le32(e + 8) != 0)
            {
                mOutDir = String(Le32(e + 4));
            }
            else if (which == kOpAssignVar && Le32(e + 12) == 0 && Le32(e + 16) == 0)
            {
                mVars[Le32(e + 4)] = String(Le32(e + 8));
            }
            else if (which == kOpExtractFile)
            {
                std::string name = String(Le32(e + 8));
                if (name.empty())
                {
                    continue;
                }
                if (name[0] != '<' && !(name.size() > 1 && name[1] == ':') && name[0] != '\\')
                {
                    name = mOutDir + "\\" + name;
                }
                if (name.compare(0, sizeof(kInstDir) - 1, kInstDir) != 0)
                {
                    continue;
                }
                std::string rel;
                for (char c : name.substr(sizeof(kInstDir) - 1))
                {
                    c = c == '\\' ? '/' : c;
                    if (c == '/' && (rel.empty() || rel.back() == '/'))
                    {
                        continue;
                    }
                    rel += c;
                }
                if (rel.empty() || rel.find('<') != std::string::npos || rel.find("..") != std::string::npos ||
                    seen.count(Lower(rel)))
                {
                    continue;
                }
                seen[Lower(rel)] = mEntries.size();
                mEntries.push_back({rel, 0});
                mFiles.push_back({Le32(e + 12)});
            }
        }
        return true;
    }

    FILE* mFile = NULL;
    uint64_t mFileSize = 0, mDataStart = 0, mDataArea = 0;
    Method mMethod = kLzma;
    bool mSolid = true, mFilterFlag = false, mUnicode = false, mNsis3 = false;
    std::vector<unsigned char> mHeader;
    uint32_t mHeaderSize = 0, mStrings = 0;
    std::string mOutDir;
    std::map<unsigned, std::string> mVars;
    std::vector<NsisFile> mFiles;
};
} // namespace

bool FindNsisHeader(FILE* f, uint64_t* pPos)
{
    const uint64_t size = FileSize64(f);
    std::vector<unsigned char> buf(1 << 16);
    for (uint64_t base = 0; base < size; base += buf.size())
    {
        const size_t n = (size_t)std::min<uint64_t>(buf.size(), size - base);
        if (!ReadAt(f, base, buf.data(), n))
        {
            return false;
        }
        for (size_t off = 0; off + 28 <= n; off += 512)
        {
            if (Le32(&buf[off + 4]) == 0xDEADBEEF && memcmp(&buf[off + 8], "NullsoftInst", 12) == 0)
            {
                *pPos = base + off;
                return true;
            }
        }
    }
    return false;
}

std::unique_ptr<Archive> OpenNsis(FILE* f, uint64_t headerPos, std::string* pError)
{
    std::unique_ptr<NsisArchive> a(new NsisArchive);
    if (!a->Init(f, headerPos, pError))
    {
        return NULL;
    }
    return a;
}
} // namespace setup
