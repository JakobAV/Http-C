#pragma once
#include "MemoryArena.h"
#include "Shared.h"

enum JsonNodeType
{
    JsonNodeType_Null,
    JsonNodeType_Object,
    JsonNodeType_Array,
    JsonNodeType_String,
    JsonNodeType_Number,
    JsonNodeType_Boolean,
};

struct JsonNode;

struct JObject
{
    // TODO: Need to give each value a key
    u32 length;
    JsonNode* values;
};

struct JArray
{
    u32 length;
    JsonNodeType arrayType;
    u8* values;
};

struct JsonNode
{
    JsonNodeType type;
    u32 hashValue;
    StringLit name;
    union
    {
        bool booleanValue;
        StringLit string;
        JObject object;
        f64 number;
        JArray array;
    };
};

// TODO: Should ManagedJsonDocument be a seperate type
struct JsonDocument
{
    MemoryArena* arena = nullptr;
    JsonNode* root;
};

JsonDocument CreateJsonDocument(MemoryArena* arena, u8* fileData, u32 length);
JsonDocument CreateManagedJsonDocument(u8* fileData, u32 length);
void FreeJsonDocument(JsonDocument doc);

JsonNode* JsonGetNode(JObject* obj, const char* name);
JsonNode* JsonGetNode(JObject* obj, u32 index);
JObject* JsonGetObject(JsonNode* node);
f64 JsonGetDouble(JsonNode* node);
f32 JsonGetFloat(JsonNode* node);
s32 JsonGetInt(JsonNode* node);
s64 JsonGetLong(JsonNode* node);
StringLit JsonGetString(JsonNode* node);
bool JsonGetBool(JsonNode* node);
JsonNodeType JsonGetType(JsonNode* node);
JsonNodeType JsonGetArrayType(JsonNode* node);
bool* JsonGetBoolArray(JsonNode* node, u32* size);
f64* JsonGetNumberArray(JsonNode* node, u32* size);
StringLit* JsonGetStringArray(JsonNode* node, u32* size);
JObject* JsonGetObjectArray(JsonNode* node, u32* size);
JArray* JsonGetArrayArray(JsonNode* node, u32* size);

bool JsonIsNull(JsonNode* node);

/*  Result from cyckle count profiling
    Read and Parse:
    Debug:
    1113078126 / JJson
    14396201527 / nlohmann
    0.07731 = 13x

    Release
    238563012 / JJson
    766485562 / nlohmann
    0.31124 = 3x

    Data Access:
    Debug:
    47481128 / JJson
    35768983 / JJson with memory alignment
    6717271489 / nlohmann
    0.00706 = 141x

    Release:
    7038319 / JJson
    6197243 / JJson with memory alignment
    1058581899 / nlohmann
    0.00664 = 150x

    Read and Parse + Data Access:
    Debug:
    1394504699 / JJson
    939403541 / JJson with memory alignment
    20952824315 / nlohmann
    0.06655 = 15x

    Release
    278914663 / JJson
    319055188 / JJson with memory alignment
    1486065129 / nlohmann
    0.18768 = 5x
 */

 /*  Result time loading level
    Debug:
    17.8281 / nlohmann (Parse ~11s, opperator[] 0.68s)*
    7.79689 / JJson (Parse 0.56s, opperator[] 0.04s)

    Release:
    4.64627 / nlohmann (Parse 0.15s, opperator[] < 0.01s)*
    4.44352 / JJson (Parse 0.06s, opperator[] < 0.01s)
    
    *Disclaimer: It wasn't easy to find all the time for nlohmann in the profiler,
                 so the total time could be a bit to low
 */