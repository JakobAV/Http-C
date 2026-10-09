#pragma once
#include "base_types.h"

#define ALIGN_TO(number, alignment) (((number) + (alignment) - 1) - (((number) + (alignment) - 1) % (alignment)))

typedef struct Arena
{
  u8 *base;
  u64 capacity;
  u64 committed;
  u64 current_offset;
  u32 alignment;
  u32 commit_granularity;
} Arena;

typedef struct TempMemory
{
  Arena *arena;
  u64 offset;
  u32 alignment;
} TempMemory;

// The *NoZero variants are only for memory the caller overwrites completely straight away
#define PushType(arena, type) ((type *)arena_push((arena), sizeof(type)))
#define PushTypeNoZero(arena, type) ((type *)arena_push_no_zero((arena), sizeof(type)))
#define PushArray(arena, type, count) ((type *)arena_push((arena), arena_array_size(sizeof(type), (count))))
#define PushArrayNoZero(arena, type, count) \
  ((type *)arena_push_no_zero((arena), arena_array_size(sizeof(type), (count))))
#define PushArrayAligned(arena, type, count, alignment) \
  ((type *)arena_push_aligned((arena), arena_array_size(sizeof(type), (count)), (alignment)))
#define PushArrayAlignedNoZero(arena, type, count, alignment) \
  ((type *)arena_push_aligned_no_zero((arena), arena_array_size(sizeof(type), (count)), (alignment)))

void arena_init(Arena *arena);
void arena_free(Arena *arena);
u64 arena_array_size(u64 element_size, u64 count);
void *arena_push_aligned_no_zero(Arena *arena, u64 size, u32 alignment);
void *arena_push_no_zero(Arena *arena, u64 size);
void *arena_push_aligned(Arena *arena, u64 size, u32 alignment);
void *arena_push(Arena *arena, u64 size);
void arena_pop_to(Arena *arena, void *pointer);
Arena *arena_get_scratch(Arena *conflict);
TempMemory temp_memory_begin(Arena *arena);
void temp_memory_end(TempMemory temp_memory);
