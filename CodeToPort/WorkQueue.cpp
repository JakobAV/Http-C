#pragma once
#include <windows.h>
#include "WorkQueue.h"

void AddEntry(WorkQueue* queue, WorkQueueCallback* callback, void* data)
{
	u32 newNextEntrytoWrite = (queue->nextEntryToWrite + 1) % ArrayCount(queue->entries);
	assert(newNextEntrytoWrite != queue->nextEntryToRead);
	WorkQueueEntry* entry = queue->entries + queue->nextEntryToWrite;
	entry->callback = callback;
	entry->data = data;
	++queue->completionGoal;
	//_WriteBarrier(); // Why no work :(
	queue->nextEntryToWrite = newNextEntrytoWrite;
	ReleaseSemaphore(queue->semaphoreHandle, 1, 0);
}

bool DoNextWorkQueueEntry(WorkQueue* queue)
{
	bool shouldSleep = false;

	u32 originalNextEntryToRead = queue->nextEntryToRead;
	u32 newNextEntryToRead = (originalNextEntryToRead + 1) % ArrayCount(queue->entries);
	if (originalNextEntryToRead != queue->nextEntryToWrite)
	{
		u32 index = InterlockedCompareExchange(&queue->nextEntryToRead, newNextEntryToRead, originalNextEntryToRead);
		if (index == originalNextEntryToRead)
		{
			// We make a copy here so that if this is a long running task it will remain in stack memory while the queue entry is used for a new task.
			// It shouldn't be needed since we are sending in a copy of the data pointer, but just to be safe
			WorkQueueEntry entry = queue->entries[index];
			entry.callback(queue, entry.data);
			InterlockedIncrement(&queue->completionCount);
		}
	}
	else
	{
		shouldSleep = true;
	}
	return shouldSleep;
}

void CompleteAllWork(WorkQueue* queue)
{
	while (queue->completionGoal != queue->completionCount)
	{
		DoNextWorkQueueEntry(queue);
	}

	queue->completionGoal = 0;
	queue->completionCount = 0;
}

DWORD WINAPI ThreadProc(void* lpParam)
{
	ThreadStartup* thread = (ThreadStartup*)lpParam;
	WorkQueue* queue = thread->queue;
	while (true)
	{
		if (DoNextWorkQueueEntry(queue))
		{
			WaitForSingleObjectEx(queue->semaphoreHandle, INFINITE, FALSE);
		}
	}
	return 0;
}

void MakeQueue(WorkQueue* queue, u32 threadCount, ThreadStartup* threadStartups)
{
	queue->completionGoal = 0;
	queue->completionCount = 0;

	queue->nextEntryToWrite = 0;
	queue->nextEntryToRead = 0;

	unsigned int initialCount = 0;
	queue->semaphoreHandle = CreateSemaphoreExA(0, initialCount, threadCount, 0, 0, SEMAPHORE_ALL_ACCESS);

	for (u32 threadIndex = 0; threadIndex < threadCount; ++threadIndex)
	{
		ThreadStartup* startup = threadStartups + threadIndex;
		startup->queue = queue;

		DWORD threadId;
		HANDLE threadHandle = CreateThread(0, 0, ThreadProc, startup, 0, &threadId);
		if (threadHandle)
		{
			CloseHandle(threadHandle);
		}
	}
}