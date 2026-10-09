#pragma once
// Must come before the first system header: -std=c11 hides madvise and MADV_* otherwise
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
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

u64 os_get_time_milliseconds(void)
{
  struct timespec time = {0};
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
  {
    // Only fails for an unsupported clock, and CLOCK_MONOTONIC always exists on Linux
    InvalidCodePath;
  }
  return (u64)time.tv_sec * 1000 + (u64)time.tv_nsec / 1000000;
}

b32 os_parse_f64(String text, f64 *result)
{
  assert(result);
  // strtod needs a null-terminated string. We never call setlocale, so '.' is the decimal point.
  char buffer[256];
  if (text.length <= 0 || text.length >= (i64)sizeof(buffer))
  {
    return false;
  }
  memcpy(buffer, text.text, text.length);
  buffer[text.length] = 0;
  char *end = null;
  errno = 0;
  f64 value = strtod(buffer, &end);
  if (end != buffer + text.length)
  {
    return false;
  }
  // Underflow is fine (rounds towards zero), overflow to infinity is not
  if (errno == ERANGE && (value > DBL_MAX || value < -DBL_MAX))
  {
    return false;
  }
  *result = value;
  return true;
}

void os_print(char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  vprintf(format, arguments);
  va_end(arguments);
  // stdout is fully buffered when it isn't a terminal, and a killed server would lose the buffered lines
  fflush(stdout);
}

u32 os_get_processor_count(void)
{
  i64 count = sysconf(_SC_NPROCESSORS_ONLN);
  return count > 0 ? (u32)count : 1;
}

typedef struct LinuxThread
{
  pthread_t handle;
  OsThreadProc *proc;
  void *data;
} LinuxThread;

static_assert(sizeof(LinuxThread) <= sizeof(OsThread), "OsThread is too small for LinuxThread");
static_assert(_Alignof(LinuxThread) <= _Alignof(OsThread), "OsThread is not aligned enough for LinuxThread");

void *linux_thread_start(void *parameter)
{
  LinuxThread *thread = parameter;
  thread->proc(thread->data);
  return null;
}

b32 os_thread_create(OsThread *thread, OsThreadProc *proc, void *data)
{
  assert(thread && proc);
  LinuxThread *linux_thread = (LinuxThread *)thread->storage;
  linux_thread->proc = proc;
  linux_thread->data = data;
  if (pthread_create(&linux_thread->handle, null, linux_thread_start, linux_thread) != 0)
  {
    return false;
  }
  pthread_detach(linux_thread->handle);
  return true;
}

static_assert(sizeof(sem_t) <= sizeof(OsSemaphore), "OsSemaphore is too small for sem_t");
static_assert(_Alignof(sem_t) <= _Alignof(OsSemaphore), "OsSemaphore is not aligned enough for sem_t");

b32 os_semaphore_init(OsSemaphore *semaphore, u32 initial_count)
{
  return sem_init((sem_t *)semaphore->storage, 0, initial_count) == 0;
}

void os_semaphore_post(OsSemaphore *semaphore)
{
  if (sem_post((sem_t *)semaphore->storage) != 0)
  {
    // Only fails for an invalid semaphore or an overflowing count
    InvalidCodePath;
  }
}

void os_semaphore_wait(OsSemaphore *semaphore)
{
  while (sem_wait((sem_t *)semaphore->storage) != 0)
  {
    if (errno != EINTR)
    {
      InvalidCodePath;
    }
  }
}

// The shared code uses plain u32s, which have the same layout as _Atomic u32 here
static_assert(sizeof(_Atomic u32) == sizeof(u32), "_Atomic u32 must have the layout of u32");

u32 atomic_load_acquire_u32(u32 volatile *value)
{
  return atomic_load_explicit((_Atomic u32 volatile *)value, memory_order_acquire);
}

void atomic_store_release_u32(u32 volatile *value, u32 new_value)
{
  atomic_store_explicit((_Atomic u32 volatile *)value, new_value, memory_order_release);
}

b32 atomic_compare_exchange_u32(u32 volatile *value, u32 expected, u32 desired)
{
  return atomic_compare_exchange_strong((_Atomic u32 volatile *)value, &expected, desired);
}

u32 atomic_increment_u32(u32 volatile *value)
{
  return atomic_fetch_add((_Atomic u32 volatile *)value, 1) + 1;
}

// Closed by the crash handler so the port is released straight away
i32 linux_listen_fd = -1;

b32 os_socket_listen(u16 port, OsSocket *result)
{
  assert(result);
  i32 fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
  {
    printf("Error opening socket: %s\n", strerror(errno));
    return false;
  }
  struct sockaddr_in socket_address = {
    .sin_port = htons(port),
    .sin_addr.s_addr = INADDR_ANY,
    .sin_family = AF_INET,
  };
  i32 option = 1;
  // Bind to the port even if an old connection on it is still in TIME_WAIT
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
  {
    printf("setsockopt failed: %s\n", strerror(errno));
    close(fd);
    return false;
  }
  if (bind(fd, (struct sockaddr *)&socket_address, sizeof(socket_address)) == -1)
  {
    printf("Error binding socket: %s\n", strerror(errno));
    close(fd);
    return false;
  }
  if (listen(fd, 64) == -1)
  {
    printf("Error listening to socket: %s\n", strerror(errno));
    close(fd);
    return false;
  }
  linux_listen_fd = fd;
  *result = (OsSocket){(u64)fd};
  return true;
}

b32 os_socket_accept(OsSocket listener, OsSocket *result)
{
  assert(result);
  while (true)
  {
    i32 fd = accept((i32)listener.handle, null, null);
    if (fd >= 0)
    {
      *result = (OsSocket){(u64)fd};
      return true;
    }
    if (errno != EINTR)
    {
      return false;
    }
  }
}

// Waits until the socket is ready for events or the deadline passes
OsSocketStatus linux_socket_wait(i32 fd, i16 events, u64 deadline_milliseconds)
{
  struct pollfd poll_fd = {
    .fd = fd,
    .events = events,
  };
  while (true)
  {
    // Recomputed on every pass, so a signal doesn't extend the deadline
    u64 now = os_get_time_milliseconds();
    u64 time_left = now < deadline_milliseconds ? deadline_milliseconds - now : 0;
    i32 ready = poll(&poll_fd, 1, time_left > INT32_MAX ? INT32_MAX : (i32)time_left);
    if (ready > 0)
    {
      // Also reported for a closed or failed connection, which the following read or send then detects
      return OsSocketStatus_Ok;
    }
    if (ready == 0)
    {
      if (time_left < INT32_MAX)
      {
        return OsSocketStatus_Timed_Out;
      }
      continue;
    }
    if (errno != EINTR)
    {
      return OsSocketStatus_Error;
    }
  }
}

OsSocketStatus os_socket_read(OsSocket socket, void *buffer, u64 size, u64 deadline_milliseconds, u64 *bytes_read)
{
  assert(bytes_read && size > 0);
  i32 fd = (i32)socket.handle;
  OsSocketStatus wait_status = linux_socket_wait(fd, POLLIN, deadline_milliseconds);
  if (wait_status != OsSocketStatus_Ok)
  {
    return wait_status;
  }
  while (true)
  {
    i64 count = read(fd, buffer, size);
    if (count > 0)
    {
      *bytes_read = (u64)count;
      return OsSocketStatus_Ok;
    }
    if (count == 0)
    {
      return OsSocketStatus_Closed;
    }
    if (errno != EINTR)
    {
      return OsSocketStatus_Error;
    }
  }
}

OsSocketStatus os_socket_send(OsSocket socket, void *buffer, u64 size, u64 deadline_milliseconds)
{
  i32 fd = (i32)socket.handle;
  u8 *at = buffer;
  while (size > 0)
  {
    // MSG_NOSIGNAL: a client that disconnects early must not kill the server with SIGPIPE.
    // MSG_DONTWAIT: a blocking send waits until the whole buffer fits, which could be forever.
    i64 bytes_sent = send(fd, at, size, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (bytes_sent >= 0)
    {
      at += bytes_sent;
      size -= bytes_sent;
      continue;
    }
    if (errno == EINTR)
    {
      continue;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
      return OsSocketStatus_Error;
    }
    OsSocketStatus wait_status = linux_socket_wait(fd, POLLOUT, deadline_milliseconds);
    if (wait_status != OsSocketStatus_Ok)
    {
      return wait_status;
    }
  }
  return OsSocketStatus_Ok;
}

void os_socket_close(OsSocket socket)
{
  close((i32)socket.handle);
}

void linux_crash_handler(i32 signal_number)
{
  (void)signal_number;
  if (linux_listen_fd != -1)
  {
    close(linux_listen_fd);
  }
  _Exit(EXIT_FAILURE);
}

void os_init(void)
{
  signal(SIGSEGV, linux_crash_handler);
  signal(SIGFPE, linux_crash_handler);
  signal(SIGABRT, linux_crash_handler);
}
