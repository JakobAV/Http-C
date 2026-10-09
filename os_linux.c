#pragma once
// Must come before the first system header: -std=c11 hides madvise and MADV_* otherwise
#define _DEFAULT_SOURCE
#include <sys/mman.h>
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
