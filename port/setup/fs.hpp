// File helpers for the setup: UTF-8 paths, case-insensitive lookups, 64-bit file offsets.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <filesystem>
#include <string>

namespace setup
{
namespace fs = std::filesystem;

fs::path PathFromUtf8(const std::string& s);
std::string PathToUtf8(const fs::path& p);
std::string Lower(std::string s);

// rel uses '/'; each component that does not exist as written is matched without regard to case.
bool FindNoCase(const fs::path& root, const std::string& rel, fs::path* pFound);

FILE* OpenFile(const fs::path& p, const char* mode);
bool Seek64(FILE* f, uint64_t pos);
uint64_t FileSize64(FILE* f);
bool ReadAt(FILE* f, uint64_t pos, void* dst, size_t size);

bool SetWorkingDir(const fs::path& p);
} // namespace setup
