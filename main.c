// The unity build root: the only translation unit, it includes every other file
// Must be the first include: os_linux.c sets feature-test macros before any system header
#if defined(__linux__)
#include "os_linux.c"
#else
#error "Unsupported platform"
#endif

#include "base_types.h"

#include "string_utils.c"
#include "memory_utils.c"
#include "http_status.c"
#include "arena.c"
#include "json.c"
#include "work_queue.c"
#include "http_server.c"
