// 7z archives through the LZMA SDK. A solid block is unpacked into memory whole.
#include "archive.hpp"

extern "C" {
#include "7z.h"
#include "7zCrc.h"
#include "7zFile.h"
}

#include <stdlib.h>

namespace setup
{
namespace
{
void* AllocFn(ISzAllocPtr, size_t size)
{
    return size ? malloc(size) : NULL;
}
void FreeFn(ISzAllocPtr, void* p)
{
    free(p);
}
const ISzAlloc kAlloc = {AllocFn, FreeFn};

const size_t kInputBufSize = 1 << 18;

class SevenZipArchive : public Archive
{
public:
    ~SevenZipArchive() override
    {
        SzArEx_Free(&mDb, &kAlloc);
        free(mLook.buf);
        if (mOpen)
        {
            File_Close(&mStream.file);
        }
    }

    bool Init(const fs::path& path, std::string* pError)
    {
        SzArEx_Init(&mDb);
        FileInStream_CreateVTable(&mStream);
        File_Construct(&mStream.file);
#if defined(_WIN32)
        if (InFile_OpenW(&mStream.file, path.c_str()) != 0)
#else
        if (InFile_Open(&mStream.file, path.c_str()) != 0)
#endif
        {
            *pError = "Cannot open the file.";
            return false;
        }
        mOpen = true;
        LookToRead2_CreateVTable(&mLook, False);
        mLook.buf = static_cast<Byte*>(malloc(kInputBufSize));
        mLook.bufSize = kInputBufSize;
        mLook.realStream = &mStream.vt;
        LookToRead2_INIT(&mLook);
        CrcGenerateTable();
        if (SzArEx_Open(&mDb, &mLook.vt, &kAlloc, &kAlloc) != SZ_OK)
        {
            *pError = "This 7z archive cannot be read (damaged, encrypted or an unsupported method).";
            return false;
        }
        std::vector<UInt16> name;
        for (UInt32 i = 0; i < mDb.NumFiles; i++)
        {
            if (SzArEx_IsDir(&mDb, i))
            {
                continue;
            }
            name.resize(SzArEx_GetFileNameUtf16(&mDb, i, NULL));
            SzArEx_GetFileNameUtf16(&mDb, i, name.data());
            mEntries.push_back({Utf16ToUtf8(name.data(), name.empty() ? 0 : name.size() - 1),
                                (uint64_t)SzArEx_GetFileSize(&mDb, i)});
            mIndices.push_back(i);
        }
        return true;
    }

    bool Extract(const std::vector<size_t>& indices, ExtractSink& sink, std::string* pError) override
    {
        UInt32 blockIndex = 0xFFFFFFFF;
        Byte* pOut = NULL;
        size_t outSize = 0;
        bool ok = true;
        for (size_t index : indices)
        {
            size_t offset = 0, processed = 0;
            if (!sink.Begin(index))
            {
                ok = false;
                break;
            }
            if (SzArEx_Extract(&mDb, &mLook.vt, mIndices[index], &blockIndex, &pOut, &outSize, &offset, &processed,
                               &kAlloc, &kAlloc) != SZ_OK)
            {
                *pError = "Cannot extract " + mEntries[index].mPath + " (damaged archive, or not enough memory).";
                ok = false;
                break;
            }
            if (!sink.Write(pOut + offset, processed) || !sink.End())
            {
                ok = false;
                break;
            }
        }
        free(pOut);
        return ok;
    }

private:
    CFileInStream mStream;
    CLookToRead2 mLook = {};
    CSzArEx mDb;
    bool mOpen = false;
    std::vector<UInt32> mIndices;
};
} // namespace

std::unique_ptr<Archive> Open7z(const fs::path& path, std::string* pError)
{
    std::unique_ptr<SevenZipArchive> a(new SevenZipArchive);
    if (!a->Init(path, pError))
    {
        return NULL;
    }
    return a;
}
} // namespace setup
