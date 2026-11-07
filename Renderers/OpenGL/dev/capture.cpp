#include "capture.hpp"
#include <stdio.h>

bool FrameCapture::WantNow(u32 frame)
{
    return frame % 30 == 0 && remove("capture_now") == 0;
}

static void Put16(FILE* f, u16 v)
{
    fwrite(&v, 2, 1, f);
}
static void Put32(FILE* f, u32 v)
{
    fwrite(&v, 4, 1, f);
}

bool FrameCapture::WriteBmp(const std::string& path, const u8* rgba, s32 width, s32 height, bool bottomUp)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
    {
        return false;
    }
    const u32 rowBytes = ((u32)width * 3 + 3) & ~3u;
    const u32 dataSize = rowBytes * (u32)height;
    fputc('B', f);
    fputc('M', f);
    Put32(f, 54 + dataSize);
    Put32(f, 0);
    Put32(f, 54);
    Put32(f, 40);
    Put32(f, (u32)width);
    Put32(f, (u32)height);
    Put16(f, 1);
    Put16(f, 24);
    Put32(f, 0);
    Put32(f, dataSize);
    Put32(f, 2835);
    Put32(f, 2835);
    Put32(f, 0);
    Put32(f, 0);
    std::vector<u8> row(rowBytes, 0);
    for (s32 y = 0; y < height; y++)
    {
        // BMP rows go bottom to top.
        const s32 srcY = bottomUp ? y : (height - 1 - y);
        const u8* src = rgba + (size_t)srcY * width * 4;
        for (s32 x = 0; x < width; x++)
        {
            row[x * 3 + 0] = src[x * 4 + 2];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 0];
        }
        fwrite(&row[0], 1, rowBytes, f);
    }
    fclose(f);
    return true;
}
