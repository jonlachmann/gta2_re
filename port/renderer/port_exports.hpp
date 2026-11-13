// Export tables of the built-in renderer and video DLLs.
#pragma once

struct PortExport
{
    const char* mName;
    void* mProc;
};

extern const PortExport kGbhExports[];
extern const int kGbhExportCount;
