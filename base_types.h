#pragma once
#include <assert.h>
#include <stdint.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef u32 b32;

static_assert(sizeof(i8) == 1 && sizeof(u8) == 1, "8-bit types must be 1 byte");
static_assert(sizeof(i16) == 2 && sizeof(u16) == 2, "16-bit types must be 2 bytes");
static_assert(sizeof(i32) == 4 && sizeof(u32) == 4, "32-bit types must be 4 bytes");
static_assert(sizeof(i64) == 8 && sizeof(u64) == 8, "64-bit types must be 8 bytes");
static_assert(sizeof(f32) == 4 && sizeof(f64) == 8, "Float types must be 4 and 8 bytes");

typedef struct String
{
  i64 length;
  char *text;
} String;

#define STR_LIT(text) (String){sizeof((text))-1, (text)}

// Compiler differences, not platform differences: these don't belong in the platform layer
#if defined(_MSC_VER)
#define Trap() __fastfail(7)
#define ThreadLocal __declspec(thread)
#define PrintfFormat(format_index, first_argument_index)
#else
#define Trap() __builtin_trap()
#define ThreadLocal _Thread_local
// Lets the compiler check format strings against their arguments
#define PrintfFormat(format_index, first_argument_index) \
  __attribute__((format(printf, format_index, first_argument_index)))
#endif

// Crashes in release builds too: if this is reached, the program state can't be trusted
#define InvalidCodePath Trap()
#define NotImplemented assert(0)
#define IsAligned(pointer, bytes) (((u64)pointer % bytes) == 0)
#define AssertAligned(pointer, bytes) assert(IsAligned(pointer, bytes))

#define ArrayCount(array) (sizeof(array) / sizeof((array)[0]))

#define KB(n) (((u64)n) << 10)
#define MB(n) (((u64)n) << 20)
#define GB(n) (((u64)n) << 30)
#define TB(n) (((u64)n) << 40)

#define false 0
#define true 1
#define null 0
