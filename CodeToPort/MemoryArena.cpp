#include "MemoryArena.h"
#include "Shared.h"
#include <windows.h>
#include <cassert>

#ifndef ARENA_DEFAULT_ALIGNMENT
#define ARENA_DEFAULT_ALIGNMENT 8
#endif

u32 GetPageSize()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}

MemoryArena* GetScratchArena()
{
    thread_local MemoryArena* tempArena = CreateArena(MB(512), 0);
    return tempArena;
}

MemoryArena* CreateArena(u64 reserveSize, u32 decommitThresholdInPageSizes)
{
    static_assert(sizeof(MemoryArena) == 64, "Ensure that we \"start\" the arena at the start of the next cache line");
    MemoryArena* arena = (MemoryArena*)VirtualAlloc(0, reserveSize, MEM_RESERVE, PAGE_NOACCESS);
    assert(arena != nullptr);
    u32 granularity = GetPageSize();
    u32 actualSize = sizeof(MemoryArena) + granularity - 1;
    actualSize -= actualSize % granularity;
    VirtualAlloc(arena, actualSize, MEM_COMMIT, PAGE_READWRITE);
    arena->base = (u8*)arena;
    arena->reservedSize = reserveSize;
    arena->currentSize = sizeof(MemoryArena);
    arena->commitSize = actualSize;
    arena->pageSize = granularity;
    arena->arenaDecomitThreshold = granularity * decommitThresholdInPageSizes;
    arena->alignment = ARENA_DEFAULT_ALIGNMENT;
    return arena;
}

void* ArenaPush(MemoryArena* arena, u32 size)
{
    void* result = nullptr;
    if (arena->currentSize + size + arena->alignment - 1 < arena->reservedSize)
    {
        u64 alignedSize = arena->currentSize + arena->alignment - 1;
        alignedSize -= alignedSize % arena->alignment;
        arena->currentSize = alignedSize;
        if (arena->currentSize + size > arena->commitSize)
        {
            u32 granularity = arena->pageSize;
            u32 actualSize = size + granularity - 1;
            actualSize -= actualSize % granularity;
            assert(arena->commitSize + actualSize <= arena->reservedSize);
            VirtualAlloc(arena->base + arena->commitSize, actualSize, MEM_COMMIT, PAGE_READWRITE);
            arena->commitSize += actualSize;
        }
        result = arena->base + arena->currentSize;
        arena->currentSize += size;
    }
    else
    {
        InvalidCodePath;
    }
    return result;
}

void* ArenaPushWithAlignment(MemoryArena* arena, u32 size, u32 alignment)
{
    u32 savedAligment = arena->alignment;
    ArenaSetAlignment(arena, alignment);
    void* result = ArenaPush(arena, size);
    ArenaSetAlignment(arena, savedAligment);
    return result;
}

void ArenaSetAlignment(MemoryArena* arena, u32 alignment)
{
    assert(alignment > 0);
    arena->alignment = alignment;
}

void ArenaPushAlignment(MemoryArena* arena)
{
    u64 alignedSize = arena->currentSize + arena->alignment - 1;
    alignedSize -= alignedSize % arena->alignment;
    arena->currentSize = alignedSize;
}

void ArenaPop(MemoryArena* arena, u32 size)
{
    assert(arena->currentSize > size + sizeof(MemoryArena));
    ArenaPopTo(arena, arena->currentSize - size);
}

void ArenaPopTo(MemoryArena* arena, u64 position)
{
    arena->currentSize = position;

    if (arena->arenaDecomitThreshold == 0)
    {
        return;
    }
    u32 pageSize = arena->pageSize;
    u64 currentSizeAllignedToPageSize = arena->currentSize + pageSize - 1;
    currentSizeAllignedToPageSize -= currentSizeAllignedToPageSize % pageSize;
    if (currentSizeAllignedToPageSize + arena->arenaDecomitThreshold <= arena->commitSize)
    {
        u64 sizeToDecommit = arena->commitSize - currentSizeAllignedToPageSize;
        /*
		This is so stupid. It is a warning about using a FEATURE of the Virtual Memory...
		It is also the whole point of this architecture, allowing us to grow and shrink the physical memory usage
		while still having the same base pointer and not having to do dome shit with copying or managing allocation blocks.
		*/
#pragma warning(push)
#pragma warning(disable : 6250)
        VirtualFree(arena->base + currentSizeAllignedToPageSize, sizeToDecommit, MEM_DECOMMIT);
#pragma warning(pop)
        arena->commitSize -= sizeToDecommit;
    }
}

TempMemory BeginTempMemory(MemoryArena* arena)
{
    TempMemory result;
    result.arena = arena;
    result.startPos = arena->currentSize;
    result.alignment = arena->alignment;
    return result;
}

void EndTempMemory(TempMemory* tempMemory)
{
    ArenaPopTo(tempMemory->arena, tempMemory->startPos);
    ArenaSetAlignment(tempMemory->arena, tempMemory->alignment);
    tempMemory->arena = nullptr;
    tempMemory->startPos = 0;
}

u8* ArenaGetCurrentPos(MemoryArena* arena)
{
    return arena->base + arena->currentSize;
}

u64 ArenaGetComitSize(MemoryArena* arena)
{
    return arena->commitSize;
}

void ArenaClear(MemoryArena* arena)
{
    ArenaPopTo(arena, sizeof(MemoryArena));
}

void FreeArena(MemoryArena* arena)
{
    VirtualFree(arena->base, 0, MEM_RELEASE);
}

void TestMemoryArena()
{
    MemoryArena* arena = CreateArena();
    int* num1 = PushType(arena, int);
    *num1 = 1;
    UseTempMemory(arena)
    {
        float* array2 = PushArray(arena, float, KB(1));
        for (int i = 0; i < KB(1); ++i)
        {
            array2[i] = i + 0.1f;
        }
    }
    TempMemory block = BeginTempMemory(arena);
    int* array1 = PushArray(arena, int, MB(1));
    for (int i = 0; i < MB(1); ++i)
    {
        array1[i] = i;
    }
    EndTempMemory(&block);

    FreeArena(arena);
}