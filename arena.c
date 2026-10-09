#pragma once
#include "base_types.h"
#include "os.h"
#include "memory_utils.c"

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

// Two per thread, so a function can use scratch memory while writing its result into the other one
ThreadLocal Arena scratch_arenas[2];

void arena_init(Arena *arena)
{
  *arena = (Arena){0};
  arena->capacity = GB(2);
  arena->alignment = 16;
  arena->commit_granularity = (u32)os_get_page_size();

  arena->base = os_reserve_memory(arena->capacity);
  if (arena->base == null)
  {
    InvalidCodePath;
  }
}

void arena_free(Arena *arena)
{
  assert(arena && arena->base);
  os_release_memory(arena->base, arena->capacity);
  *arena = (Arena){0};
}

// Array counts can come from untrusted input, so a size that would wrap around must not become a small push
u64 arena_array_size(u64 element_size, u64 count)
{
  if (element_size != 0 && count > UINT64_MAX / element_size)
  {
    InvalidCodePath;
  }
  return element_size * count;
}

// Aligns the start of the allocation. arena->alignment is left unchanged.
void *arena_push_aligned_no_zero(Arena *arena, u64 size, u32 alignment)
{
  assert(arena);
  assert(alignment > 0 && (alignment & (alignment - 1)) == 0);
  if (arena->base == null)
  {
    arena_init(arena);
  }
  u64 start = ALIGN_TO(arena->current_offset, (u64)alignment);
  // Written as a subtraction so a huge size can't wrap around the check
  if (start > arena->capacity || size > arena->capacity - start)
  {
    InvalidCodePath;
  }
  u64 end = start + size;
  if (end > arena->committed)
  {
    u64 commit_size = ALIGN_TO(end - arena->committed, (u64)arena->commit_granularity);
    if (!os_commit_memory(arena->base + arena->committed, commit_size))
    {
      InvalidCodePath;
    }
    arena->committed += commit_size;
  }
  arena->current_offset = end;
  return arena->base + start;
}

void *arena_push_no_zero(Arena *arena, u64 size)
{
  assert(arena);
  if (arena->base == null)
  {
    arena_init(arena);
  }
  return arena_push_aligned_no_zero(arena, size, arena->alignment);
}

void *arena_push_aligned(Arena *arena, u64 size, u32 alignment)
{
  void *result = arena_push_aligned_no_zero(arena, size, alignment);
  // Decommitted pages can come back holding old data
  memory_set(result, 0, size);
  return result;
}

void *arena_push(Arena *arena, u64 size)
{
  void *result = arena_push_no_zero(arena, size);
  memory_set(result, 0, size);
  return result;
}

void arena_pop_to(Arena *arena, void *pointer)
{
  assert(arena);
  assert(((u64)pointer) >= (u64)arena->base);
  u64 new_offset = ((u64)pointer) - (u64)arena->base;
  assert(new_offset <= arena->current_offset);

  arena->current_offset = new_offset;
  if (new_offset + arena->commit_granularity * 4 < arena->committed)
  {
    u64 new_committed = ALIGN_TO(new_offset, (u64)arena->commit_granularity);
    u64 size = arena->committed - new_committed;
    void *target = arena->base + new_committed;
    if (!os_decommit_memory(target, size))
    {
      InvalidCodePath;
    }
    arena->committed = new_committed;
  }
}

// Returns the calling thread's scratch arena that isn't conflict. Pass the arena the caller writes its
// output into as conflict (it may itself be a scratch arena), or null if there is none.
Arena *arena_get_scratch(Arena *conflict)
{
  if (conflict == &scratch_arenas[0])
  {
    return &scratch_arenas[1];
  }
  return &scratch_arenas[0];
}

TempMemory temp_memory_begin(Arena *arena)
{
  assert(arena);
  if (arena->base == null)
  {
    arena_init(arena);
  }
  TempMemory result = {
    .arena = arena,
    .offset = arena->current_offset,
    .alignment = arena->alignment,
  };

  return result;
}

void temp_memory_end(TempMemory temp_memory)
{
  arena_pop_to(temp_memory.arena, temp_memory.arena->base + temp_memory.offset);
  temp_memory.arena->alignment = temp_memory.alignment;
}
