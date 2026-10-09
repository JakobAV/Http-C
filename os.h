#pragma once
#include "base_types.h"
#include "arena.h"

// Platform API. Implemented by os_<platform>.c, the only files allowed to call the OS or libc.

// Virtual memory. Sizes and addresses passed to commit/decommit must be multiples of the page size.
u64 os_get_page_size(void);
// Reserves address space without backing memory. Returns null on failure.
void *os_reserve_memory(u64 size);
b32 os_commit_memory(void *memory, u64 size);
// Returns the pages to the OS and makes them inaccessible. Recommitted pages may hold old data or zeros.
b32 os_decommit_memory(void *memory, u64 size);
void os_release_memory(void *memory, u64 size);

// Files
// Reads the whole file into arena. Returns false if it can't be opened or read (missing file, permissions, ...).
b32 os_read_entire_file(Arena *arena, String path, String *result);

// Number parsing
// Converts the whole text to a f64, with '.' as the decimal point. Returns false if the text isn't a number,
// is too long or is out of range. Callers validate their own number syntax first.
b32 os_parse_f64(String text, f64 *result);

// Console output
void os_print(char *format, ...) PrintfFormat(1, 2);
