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

// Threads and synchronization
typedef void OsThreadProc(void *data);
// Opaque storage for the OS objects. An OsThread must stay alive (not move) while its thread runs.
typedef struct OsThread
{
  _Alignas(8) u8 storage[32];
} OsThread;
typedef struct OsSemaphore
{
  _Alignas(8) u8 storage[32];
} OsSemaphore;

// Returns at least 1
u32 os_get_processor_count(void);
// Starts a detached thread that runs proc(data)
b32 os_thread_create(OsThread *thread, OsThreadProc *proc, void *data);
b32 os_semaphore_init(OsSemaphore *semaphore, u32 initial_count);
void os_semaphore_post(OsSemaphore *semaphore);
void os_semaphore_wait(OsSemaphore *semaphore);

// Atomics. CPU and compiler features rather than OS ones, hence no os_ prefix. The type suffix and ordering
// in each name keep them from clashing with <stdatomic.h>. Operations without an ordering in the name are
// sequentially consistent.
u32 atomic_load_acquire_u32(u32 volatile *value);
void atomic_store_release_u32(u32 volatile *value, u32 new_value);
// Sets *value to desired if it equals expected. Returns whether it did.
b32 atomic_compare_exchange_u32(u32 volatile *value, u32 expected, u32 desired);
// Returns the new value
u32 atomic_increment_u32(u32 volatile *value);

// Sockets (TCP). Errors are reported through return values and the socket is never left half-open.
typedef struct OsSocket
{
  u64 handle;
} OsSocket;

// Listens on every interface. Prints the reason and returns false on failure.
b32 os_socket_listen(u16 port, OsSocket *result);
b32 os_socket_accept(OsSocket listener, OsSocket *result);
// Returns the number of bytes read, 0 when the peer has closed the connection, or -1 on an error
i64 os_socket_read(OsSocket socket, void *buffer, u64 size);
// Sends all of buffer. Returns false if the connection failed. Never raises SIGPIPE or an equivalent.
b32 os_socket_send(OsSocket socket, void *buffer, u64 size);
void os_socket_close(OsSocket socket);

// Entry point. The platform's main sets up the process and then calls app_run, which the application
// defines. Its return value is the process exit code.
i32 app_run(void);
