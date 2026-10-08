#pragma once
#include "Shared.h"

struct MemoryArena
{
    u8* base;
    u64 reservedSize;
    u64 currentSize;
    u64 commitSize;
    u32 pageSize;
    u32 arenaDecomitThreshold;
    u32 alignment;
    u32 _unused[5]; // To make the struct 64 bytes (one cache line)
};

struct TempMemory
{
    MemoryArena* arena;
    u64 startPos;
    u32 alignment;
};

#define PushType(arena, type) (type*)ArenaPush(arena, sizeof(type))
#define PushArray(arena, type, count) (type*)ArenaPush(arena, sizeof(type) * count)
#define PushTypeAligned(arena, type, aligment) (type*)ArenaPushWithAlignment(arena, sizeof(type), aligment)
#define PushArrayAligned(arena, type, count, aligment) (type*)ArenaPushWithAlignment(arena, sizeof(type) * count, aligment)
#define PopType(arena, type) ArenaPop(arena, sizeof(type))
#define PopArray(arena, type, count) ArenaPop(arena, sizeof(type) * count)
#define UseTempMemory(in_arena) for (TempMemory _block_ = BeginTempMemory(in_arena); _block_.arena!=nullptr; EndTempMemory(&_block_))

MemoryArena* GetScratchArena();
MemoryArena* CreateArena(u64 reserveSize = GB(4), u32 decommitThresholdInPageSizes = 4);
void* ArenaPush(MemoryArena* arena, u32 size);
void* ArenaPushWithAlignment(MemoryArena* arena, u32 size, u32 alignment);
void ArenaSetAlignment(MemoryArena* arena, u32 alignment);
void ArenaPushAlignment(MemoryArena* arena);
void ArenaPop(MemoryArena* arena, u32 size);
void ArenaPopTo(MemoryArena* arena, u64 position);
void ArenaClear(MemoryArena* arena);
u8* ArenaGetCurrentPos(MemoryArena* arena);
u64 ArenaGetComitSize(MemoryArena* arena);
TempMemory BeginTempMemory(MemoryArena* arena);
void EndTempMemory(TempMemory* tempMemory);
void FreeArena(MemoryArena* arena);

