#pragma once

typedef unsigned char u8;
typedef signed char s8;
typedef char char_type;

typedef unsigned short int u16;
typedef signed short int s16;

typedef unsigned int u32;
typedef signed int s32;

typedef float f32;
typedef double f64;

// The 64-bit port (docs/port.md, macOS): pointers are 8 bytes, where the original and the 32-bit port builds have 4
#if defined(GTA2_PORT) && (defined(__LP64__) || defined(_WIN64))
    #define GTA2_PORT_64
#endif

// An integer that holds a pointer: u32/s32 in the 32-bit original, pointer-sized in the 64-bit port
#if defined(GTA2_PORT_64)
typedef unsigned long long uptr;
typedef signed long long sptr;
#else
typedef u32 uptr;
typedef s32 sptr;
#endif
