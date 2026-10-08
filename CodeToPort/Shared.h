#pragma once
#include <cassert>

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;

typedef signed char         s8;
typedef signed short        s16;
typedef signed int          s32;
typedef signed long long    s64;

typedef float f32;
typedef double f64;

struct StringLit
{
    u32 length;
    const char* text;
};
#define STR_LIT(text) StringLit{sizeof(text)-1, text}

#define ArrayCount(arr) (sizeof(arr) / sizeof(arr[0]))

#define KB(n) (n << 10)
#define MB(n) (n << 20)
#define GB(n) (((u64)n) << 30)
#define TB(n) (((u64)n) << 40)

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) < (b) ? (b) : (a)
#endif
#ifndef clamp
#define clamp(v, minimum, maximum) (max(min(v, minimum), maximum))
#endif

#define InvalidCodePath assert(false)
#define NotImplemented assert(false)
#define IsAligned(pointer, bytes) (((u64)pointer % bytes) == 0)
#define AssertAligned(pointer, bytes) assert(IsAligned(pointer, bytes))