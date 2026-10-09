#pragma once
#include "base_types.h"
#include "os.h"

// A ring buffer of work with one producer and any number of worker threads. The queue holds at most
// WORK_QUEUE_SIZE - 1 entries.
#ifndef WORK_QUEUE_SIZE
#define WORK_QUEUE_SIZE 1024
#endif

typedef struct WorkQueue WorkQueue;
typedef void WorkQueueCallback(WorkQueue *queue, void *data);

typedef struct WorkQueueEntry
{
  WorkQueueCallback *callback;
  void *data;
  // Set by the producer, cleared by the worker that claimed the entry once it has copied it
  u32 volatile in_use;
} WorkQueueEntry;

struct WorkQueue
{
  // Only the producer changes completion_goal and next_entry_to_write
  u32 volatile completion_goal;
  u32 volatile completion_count;
  u32 volatile next_entry_to_write;
  // Workers claim entries by advancing this with a compare-exchange
  u32 volatile next_entry_to_read;
  OsSemaphore semaphore;
  WorkQueueEntry entries[WORK_QUEUE_SIZE];
};

// Single producer only: every call must come from the same thread.
// Returns false, without adding the entry, if the queue is full.
b32 work_queue_add_entry(WorkQueue *queue, WorkQueueCallback *callback, void *data)
{
  assert(queue && callback);
  u32 write_index = queue->next_entry_to_write;
  u32 next_write_index = (write_index + 1) % WORK_QUEUE_SIZE;
  if (next_write_index == atomic_load_acquire_u32(&queue->next_entry_to_read))
  {
    return false;
  }
  WorkQueueEntry *entry = queue->entries + write_index;
  // A worker can have claimed the slot without having copied it yet. Acquire: its copy is then complete.
  if (atomic_load_acquire_u32(&entry->in_use))
  {
    return false;
  }
  entry->callback = callback;
  entry->data = data;
  atomic_store_release_u32(&entry->in_use, 1);
  queue->completion_goal++;
  // Release: the entry must be written before a worker can see the new index
  atomic_store_release_u32(&queue->next_entry_to_write, next_write_index);
  os_semaphore_post(&queue->semaphore);
  return true;
}

// Runs one entry on the calling thread. Returns false if there was no work.
b32 work_queue_do_next_entry(WorkQueue *queue)
{
  u32 read_index = atomic_load_acquire_u32(&queue->next_entry_to_read);
  if (read_index == atomic_load_acquire_u32(&queue->next_entry_to_write))
  {
    return false;
  }
  if (atomic_compare_exchange_u32(&queue->next_entry_to_read, read_index, (read_index + 1) % WORK_QUEUE_SIZE))
  {
    // Copy the entry and release the slot before running it, so a long task doesn't keep the slot busy
    WorkQueueEntry *slot = queue->entries + read_index;
    WorkQueueCallback *callback = slot->callback;
    void *data = slot->data;
    atomic_store_release_u32(&slot->in_use, 0);
    callback(queue, data);
    atomic_increment_u32(&queue->completion_count);
  }
  return true;
}

// Producer only. Helps with the work until everything that has been added is done, then resets the counters.
// The reset is racy if work is added while it happens, which a single producer can't do.
void work_queue_complete_all(WorkQueue *queue)
{
  while (queue->completion_goal != atomic_load_acquire_u32(&queue->completion_count))
  {
    work_queue_do_next_entry(queue);
  }
  queue->completion_goal = 0;
  atomic_store_release_u32(&queue->completion_count, 0);
}

void work_queue_thread_proc(void *data)
{
  WorkQueue *queue = data;
  while (true)
  {
    if (!work_queue_do_next_entry(queue))
    {
      os_semaphore_wait(&queue->semaphore);
    }
  }
}

// threads must hold thread_count elements and outlive the queue. The worker threads never exit.
b32 work_queue_init(WorkQueue *queue, u32 thread_count, OsThread *threads)
{
  assert(queue && (threads || thread_count == 0));
  queue->completion_goal = 0;
  queue->completion_count = 0;
  queue->next_entry_to_write = 0;
  queue->next_entry_to_read = 0;
  for (u32 i = 0; i < WORK_QUEUE_SIZE; ++i)
  {
    queue->entries[i].in_use = 0;
  }
  if (!os_semaphore_init(&queue->semaphore, 0))
  {
    return false;
  }
  for (u32 i = 0; i < thread_count; ++i)
  {
    if (!os_thread_create(threads + i, work_queue_thread_proc, queue))
    {
      return false;
    }
  }
  return true;
}
