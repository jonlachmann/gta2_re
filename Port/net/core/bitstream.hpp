// Port/net core (docs/netcode.md, "Wire format"): a little-endian bit stream. The reader never reads past its end:
// it returns zeros and remembers the overflow, so a malformed packet from a client is rejected, not trusted.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace net
{
class BitWriter
{
  public:
    void Bits(uint32_t value, int count); // the low <count> bits (0..32)
    void Bool(bool value)
    {
        Bits(value ? 1 : 0, 1);
    }
    void VarU(uint32_t value); // 7 bits per step, small values small
    void VarS(int32_t value); // zigzag, then VarU
    void Bytes(const void* data, size_t size); // byte-aligns first

    const std::vector<uint8_t>& Data() const
    {
        return mData;
    }
    size_t BitCount() const
    {
        return mBits;
    }
    size_t ByteCount() const
    {
        return (mBits + 7) / 8;
    }

  private:
    std::vector<uint8_t> mData;
    size_t mBits = 0;
};

class BitReader
{
  public:
    BitReader(const void* data, size_t size) : mData((const uint8_t*)data), mSize(size)
    {
    }

    uint32_t Bits(int count);
    bool Bool()
    {
        return Bits(1) != 0;
    }
    uint32_t VarU();
    int32_t VarS();
    bool Bytes(void* out, size_t size);

    bool Overflow() const
    {
        return mOverflow;
    }
    size_t BitsLeft() const
    {
        return mOverflow ? 0 : mSize * 8 - mBits;
    }

  private:
    const uint8_t* mData;
    size_t mSize;
    size_t mBits = 0;
    bool mOverflow = false;
};
} // namespace net
