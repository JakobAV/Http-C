#pragma once
#include <stdio.h>
#include <stdlib.h>
#include "Shared.h"


static u8* ReadEntireFile(const char* fileName, u32& bytesRead)
{
    FILE* fileHandle;
    fopen_s(&fileHandle, fileName, "r");
    if (fileHandle == nullptr)
    {
        return nullptr;
    }
    if (fseek(fileHandle, 0, SEEK_END) != 0)
    {
        fclose(fileHandle);
        return nullptr;
    }
    int fileSize = ftell(fileHandle);
    if (fileSize == 0)
    {
        fclose(fileHandle);
        return nullptr;
    }
    fseek(fileHandle, 0, SEEK_SET);
    u8* buffer = (u8*)malloc(fileSize);
    if (buffer == 0)
    {
        fclose(fileHandle);
        return nullptr;
    }
    bytesRead = (u32)fread_s(buffer, fileSize, sizeof(char), fileSize, fileHandle);
    fclose(fileHandle);
    return buffer;
}