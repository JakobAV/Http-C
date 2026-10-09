#pragma once
#include "base_types.h"

void memory_copy(void *destination, void *source, u64 size)
{
  assert((destination && source) || size == 0);
  u8 *to = destination;
  u8 *from = source;
  for (u64 i = 0; i < size; ++i)
  {
    to[i] = from[i];
  }
}

void memory_set(void *destination, u8 value, u64 size)
{
  assert(destination || size == 0);
  u8 *to = destination;
  for (u64 i = 0; i < size; ++i)
  {
    to[i] = value;
  }
}
