#pragma once
#include <assert.h>

typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long int u32;
typedef unsigned long long u64;

typedef float f32;
typedef double f64;

typedef u32 b32;

typedef struct String
{
  i64 length;
  char* text;
} String;

#define STR_LIT(text) (String){sizeof((text))-1, (text)}

#define InvalidCodePath assert(0)
#define NotImplemented assert(0)
#define IsAligned(pointer, bytes) (((u64)pointer % bytes) == 0)
#define AssertAligned(pointer, bytes) assert(IsAligned(pointer, bytes))

#define KB(n) (((u64)n) << 10)
#define MB(n) (((u64)n) << 20)
#define GB(n) (((u64)n) << 30)
#define TB(n) (((u64)n) << 40)

#define false 0
#define true 1
#define null 0