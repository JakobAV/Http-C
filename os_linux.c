#pragma once
// Must come before the first system header: -std=c11 hides madvise and MADV_* otherwise
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base_types.h"
#include "os.h"

u64 os_get_page_size(void)
{
  i64 page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0)
  {
    InvalidCodePath;
  }
  return (u64)page_size;
}

void *os_reserve_memory(u64 size)
{
  void *memory = mmap(null, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (memory == MAP_FAILED)
  {
    return null;
  }
  return memory;
}

b32 os_commit_memory(void *memory, u64 size)
{
  return mprotect(memory, size, PROT_READ | PROT_WRITE) == 0;
}

// mprotect alone keeps the pages resident. MADV_FREE lets the kernel reclaim them lazily, under memory
// pressure, which is much cheaper than MADV_DONTNEED for memory that is reused soon.
b32 os_decommit_memory(void *memory, u64 size)
{
  if (madvise(memory, size, MADV_FREE) != 0)
  {
    return false;
  }
  return mprotect(memory, size, PROT_NONE) == 0;
}

void os_release_memory(void *memory, u64 size)
{
  munmap(memory, size);
}

b32 os_read_entire_file(Arena *arena, String path, String *result)
{
  assert(arena && result);
  *result = (String){0};
  // open needs a null-terminated path
  Arena *scratch = arena_get_scratch(arena);
  TempMemory temp_memory = temp_memory_begin(scratch);
  char *terminated_path = PushArrayNoZero(scratch, char, (u64)path.length + 1);
  memcpy(terminated_path, path.text, path.length);
  terminated_path[path.length] = 0;
  i32 fd = -1;
  do
  {
    fd = open(terminated_path, O_RDONLY | O_CLOEXEC);
  } while (fd < 0 && errno == EINTR);
  temp_memory_end(temp_memory);
  if (fd < 0)
  {
    return false;
  }

  struct stat file_info = {0};
  if (fstat(fd, &file_info) != 0 || !S_ISREG(file_info.st_mode))
  {
    close(fd);
    return false;
  }
  u64 size = (u64)file_info.st_size;
  char *buffer = PushArrayNoZero(arena, char, size);
  u64 bytes_read = 0;
  b32 success = true;
  while (bytes_read < size)
  {
    i64 count = read(fd, buffer + bytes_read, size - bytes_read);
    if (count < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      success = false;
      break;
    }
    if (count == 0)
    {
      // The file shrank after fstat
      break;
    }
    bytes_read += count;
  }
  close(fd);
  if (!success)
  {
    arena_pop_to(arena, buffer);
    return false;
  }
  *result = (String){(i64)bytes_read, buffer};
  return true;
}
