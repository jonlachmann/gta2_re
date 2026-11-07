// Port/net core: see bitstream.hpp.
#include "bitstream.hpp"
#include <string.h>

void net::BitWriter::Bits(uint32_t value, int count)
{
    for (int i = 0; i < count; i++)
    {
        if ((mBits & 7) == 0)
        {
            mData.push_back(0);
        }
        if ((value >> i) & 1)
        {
            mData[mBits >> 3] |= (uint8_t)(1u << (mBits & 7));
        }
        mBits++;
    }
}

void net::BitWriter::VarU(uint32_t value)
{
    do
    {
        const uint32_t part = value & 0x7F;
        value >>= 7;
        Bits(part | (value ? 0x80 : 0), 8);
    } while (value);
}

void net::BitWriter::VarS(int32_t value)
{
    VarU(((uint32_t)value << 1) ^ (uint32_t)(value >> 31));
}

void net::BitWriter::Bytes(const void* data, size_t size)
{
    mBits = (mBits + 7) & ~(size_t)7;
    const size_t at = mBits / 8;
    mData.resize(at + size);
    memcpy(&mData[at], data, size);
    mBits += size * 8;
}

uint32_t net::BitReader::Bits(int count)
{
    if (mOverflow || mBits + count > mSize * 8)
    {
        mOverflow = true;
        return 0;
    }
    uint32_t value = 0;
    for (int i = 0; i < count; i++)
    {
        value |= (uint32_t)((mData[mBits >> 3] >> (mBits & 7)) & 1) << i;
        mBits++;
    }
    return value;
}

uint32_t net::BitReader::VarU()
{
    uint32_t value = 0;
    for (int shift = 0; shift < 35; shift += 7)
    {
        const uint32_t part = Bits(8);
        value |= (part & 0x7F) << shift;
        if (!(part & 0x80))
        {
            return value;
        }
    }
    mOverflow = true; // more than 5 steps: not ours
    return 0;
}

int32_t net::BitReader::VarS()
{
    const uint32_t z = VarU();
    return (int32_t)((z >> 1) ^ (0u - (z & 1)));
}

bool net::BitReader::Bytes(void* out, size_t size)
{
    mBits = (mBits + 7) & ~(size_t)7;
    if (mOverflow || mBits / 8 + size > mSize)
    {
        mOverflow = true;
        memset(out, 0, size);
        return false;
    }
    memcpy(out, mData + mBits / 8, size);
    mBits += size * 8;
    return true;
}
