#include "base_types.h"
#include <sys/mman.h>

#define ALIGN_TO(number, alignment) (((number) + (alignment) - 1) - (((number) + (alignment) - 1) % alignment))

typedef struct Arena
{
  u8* base;
  u64 capacity;
  u64 commited;
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

void arena_init(Arena *arena)
{
  *arena = (Arena){0};
  arena->capacity = GB(2);
  arena->alignment = 16;
  arena->commit_granularity = KB(4);

  arena->base = mmap(null, arena->capacity, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
}

void arena_free(Arena *arena)
{
  assert(arena && arena->base);
  munmap(arena->base, arena->capacity);
}

void* arena_push(Arena *arena, u64 size)
{
  assert(arena);
  if(arena->base == null)
  {
    arena_init(arena);
  }
  u64 new_offset = ALIGN_TO(arena->current_offset + size, arena->alignment);
  assert(new_offset <= arena->capacity);
  if(new_offset > arena->commited)
  {
    void *new_allocation_start = arena->base + arena->commited;
    u64 commit_size = ALIGN_TO(new_offset - arena->commited, arena->commit_granularity);
    i32 result = mprotect(new_allocation_start, commit_size, PROT_READ | PROT_WRITE);
    assert(result == 0);
    arena->commited += commit_size;
  }
  void *current_position = arena->base + arena->current_offset;
  arena->current_offset = new_offset;
  return current_position;
}

void arena_pop_to(Arena *arena, void *pointer)
{
  assert(arena);
  assert(((u64)pointer) >= (u64)arena->base);
  u64 new_offset = ((u64)pointer) - (u64)arena->base;
  assert(new_offset < arena->commited);

  arena->current_offset = new_offset;
  if(new_offset + arena->commit_granularity * 4 < arena->commited)
  {
    u64 new_commited = ALIGN_TO(new_offset, arena->commit_granularity);
    u64 size = arena->commited - new_commited;
    void *target = arena->base + new_commited;
    i32 result = mprotect(target, size, PROT_NONE);
    assert(result == 0);
    arena->commited = new_commited;
  }
}

TempMemory temp_memory_begin(Arena *arena)
{
  assert(arena);
  if(arena->base == null)
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