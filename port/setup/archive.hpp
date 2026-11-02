// Read-only access to the files inside an archive, a disc image or an installer. Nothing is run.
#pragma once

#include "fs.hpp"

#include <memory>
#include <vector>

namespace setup
{
struct ArchiveEntry
{
    std::string mPath; // '/' separated, as stored
    uint64_t mSize;
};

// Receives the extracted files; returning false stops the extraction.
class ExtractSink
{
public:
    virtual ~ExtractSink() {}
    virtual bool Begin(size_t index) = 0;
    virtual bool Write(const void* pData, size_t size) = 0;
    virtual bool End() = 0;
};

class Archive
{
public:
    virtual ~Archive() {}
    const std::vector<ArchiveEntry>& Entries() const { return mEntries; }
    // Extracts the given entries (indices in ascending order) and calls the sink for each.
    virtual bool Extract(const std::vector<size_t>& indices, ExtractSink& sink, std::string* pError) = 0;

protected:
    std::vector<ArchiveEntry> mEntries;
};

// The format comes from the file's content, not its name. On failure *pError says why, in words
// for the player.
std::unique_ptr<Archive> OpenArchive(const fs::path& path, std::string* pError);

std::unique_ptr<Archive> OpenZip(FILE* f, std::string* pError);
std::unique_ptr<Archive> Open7z(const fs::path& path, std::string* pError);
std::unique_ptr<Archive> OpenIso(FILE* f, uint32_t sectorSize, uint32_t dataOffset, std::string* pError);
// NSIS installer whose first header is at that offset.
std::unique_ptr<Archive> OpenNsis(FILE* f, uint64_t headerPos, std::string* pError);
bool FindNsisHeader(FILE* f, uint64_t* pPos);

std::string Utf16ToUtf8(const uint16_t* p, size_t count);
} // namespace setup
