#pragma once
#include "base_types.h"
#include "arena.h"
#include "os.h"
#include "memory_utils.c"
#include "string_utils.c"

// Deeper input is rejected instead of overflowing the stack through recursion
#define JSON_MAX_DEPTH 64

typedef enum JsonNodeType
{
  JsonNodeType_None = 0,
  JsonNodeType_Null,
  JsonNodeType_Object,
  JsonNodeType_Array,
  JsonNodeType_String,
  JsonNodeType_Number,
  JsonNodeType_Boolean,
} JsonNodeType;

// What JsonArray.values points to. Arrays whose elements all have the same type are stored compactly.
typedef enum JsonArrayType
{
  JsonArrayType_Empty = 0, // values is null
  JsonArrayType_Null,      // values is null, every element is null
  JsonArrayType_Object,    // JsonObject[]
  JsonArrayType_Array,     // JsonArray[]
  JsonArrayType_String,    // String[]
  JsonArrayType_Number,    // f64[]
  JsonArrayType_Boolean,   // b32[]
  JsonArrayType_Mixed,     // JsonNode[] without names, when the elements have different types
} JsonArrayType;

#define JSON_ERROR_LIST \
  X(None, "No error") \
  X(Unexpected_End, "Unexpected end of input") \
  X(Unexpected_Token, "Unexpected token") \
  X(Invalid_Character, "Invalid character") \
  X(Invalid_Literal, "Invalid literal") \
  X(Invalid_Number, "Invalid number") \
  X(Invalid_String, "Invalid string") \
  X(Invalid_Escape, "Invalid escape sequence") \
  X(Too_Deep, "Nesting is too deep") \
  X(Too_Large, "Input is too large") \
  X(Trailing_Characters, "Unexpected characters after the value")

typedef enum JsonError
{
#define X(name, _) JsonError_##name,
  JSON_ERROR_LIST
#undef X
} JsonError;

typedef struct JsonNode JsonNode;

typedef struct JsonObject
{
  u32 length;
  JsonNode *values;
} JsonObject;

typedef struct JsonArray
{
  u32 length;
  JsonArrayType type;
  void *values;
} JsonArray;

struct JsonNode
{
  JsonNodeType type;
  // name and name_hash are only set for object members
  u32 name_hash;
  String name;
  union
  {
    b32 boolean;
    f64 number;
    String string;
    JsonObject object;
    JsonArray array;
  };
};

// Strings without escapes point into the parsed text, so the text must outlive the document.
typedef struct JsonDocument
{
  JsonNode *root;
  JsonError error;
  i64 error_position;
} JsonDocument;

typedef enum JsonTokenType
{
  JsonTokenType_None = 0,
  JsonTokenType_Open_Brace,
  JsonTokenType_Close_Brace,
  JsonTokenType_Open_Bracket,
  JsonTokenType_Close_Bracket,
  JsonTokenType_Comma,
  JsonTokenType_Colon,
  JsonTokenType_String,
  JsonTokenType_Number,
  JsonTokenType_True,
  JsonTokenType_False,
  JsonTokenType_Null,
  JsonTokenType_End_Of_Input,
} JsonTokenType;

typedef struct JsonToken
{
  JsonTokenType type;
  // For strings, the text between the quotes, still escaped
  String text;
  b32 has_escapes;
  i64 position;
} JsonToken;

typedef struct JsonParser
{
  String text;
  i64 position;
  Arena *arena;
  Arena *scratch;
  u32 depth;
  JsonError error;
  i64 error_position;
} JsonParser;

String json_error_to_string(JsonError error)
{
  switch (error)
  {
#define X(name, message) case JsonError_##name: return STR_LIT(message);
    JSON_ERROR_LIST
#undef X
    default: InvalidCodePath;
  }
}

// Jenkins one-at-a-time
u32 json_hash_string(String string)
{
  u32 hash = 0;
  for (i64 i = 0; i < string.length; ++i)
  {
    hash += (u8)string.text[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}

// The first error wins
void json_parser_set_error(JsonParser *parser, JsonError error, i64 position)
{
  if (!parser->error)
  {
    parser->error = error;
    parser->error_position = position;
  }
}

b32 json_char_is_digit(char c)
{
  return c >= '0' && c <= '9';
}

// Returns -1 for a character that isn't a hex digit
i32 json_hex_value(char c)
{
  if (c >= '0' && c <= '9')
  {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f')
  {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F')
  {
    return c - 'A' + 10;
  }
  return -1;
}

b32 json_parser_at_digit(JsonParser *parser)
{
  return parser->position < parser->text.length && json_char_is_digit(parser->text.text[parser->position]);
}

void json_skip_digits(JsonParser *parser)
{
  while (json_parser_at_digit(parser))
  {
    parser->position++;
  }
}

// Validates the JSON number grammar: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
void json_scan_number(JsonParser *parser, JsonToken *token)
{
  String text = parser->text;
  i64 start = parser->position;
  if (text.text[parser->position] == '-')
  {
    parser->position++;
  }
  if (!json_parser_at_digit(parser))
  {
    json_parser_set_error(parser, JsonError_Invalid_Number, start);
    return;
  }
  if (text.text[parser->position] == '0')
  {
    parser->position++;
    if (json_parser_at_digit(parser))
    {
      // Leading zeros aren't allowed
      json_parser_set_error(parser, JsonError_Invalid_Number, start);
      return;
    }
  }
  else
  {
    json_skip_digits(parser);
  }
  if (parser->position < text.length && text.text[parser->position] == '.')
  {
    parser->position++;
    if (!json_parser_at_digit(parser))
    {
      json_parser_set_error(parser, JsonError_Invalid_Number, start);
      return;
    }
    json_skip_digits(parser);
  }
  if (parser->position < text.length && (text.text[parser->position] == 'e' || text.text[parser->position] == 'E'))
  {
    parser->position++;
    if (parser->position < text.length && (text.text[parser->position] == '+' || text.text[parser->position] == '-'))
    {
      parser->position++;
    }
    if (!json_parser_at_digit(parser))
    {
      json_parser_set_error(parser, JsonError_Invalid_Number, start);
      return;
    }
    json_skip_digits(parser);
  }
  token->type = JsonTokenType_Number;
  token->text = (String){parser->position - start, text.text + start};
}

// Finds the closing quote and validates escapes. Decoding happens later, in json_decode_string.
void json_scan_string(JsonParser *parser, JsonToken *token)
{
  String text = parser->text;
  i64 start = ++parser->position;
  while (true)
  {
    if (parser->position >= text.length)
    {
      json_parser_set_error(parser, JsonError_Invalid_String, start - 1);
      return;
    }
    u8 c = (u8)text.text[parser->position];
    if (c == '"')
    {
      break;
    }
    if (c < 0x20)
    {
      // Control characters must be escaped
      json_parser_set_error(parser, JsonError_Invalid_String, parser->position);
      return;
    }
    if (c == '\\')
    {
      token->has_escapes = true;
      i64 escape_start = parser->position++;
      if (parser->position >= text.length)
      {
        json_parser_set_error(parser, JsonError_Invalid_String, start - 1);
        return;
      }
      char escape = text.text[parser->position];
      if (escape == 'u')
      {
        for (i32 i = 0; i < 4; ++i)
        {
          parser->position++;
          if (parser->position >= text.length || json_hex_value(text.text[parser->position]) < 0)
          {
            json_parser_set_error(parser, JsonError_Invalid_Escape, escape_start);
            return;
          }
        }
      }
      else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' && escape != 'f' &&
               escape != 'n' && escape != 'r' && escape != 't')
      {
        json_parser_set_error(parser, JsonError_Invalid_Escape, escape_start);
        return;
      }
    }
    parser->position++;
  }
  token->type = JsonTokenType_String;
  token->text = (String){parser->position - start, text.text + start};
  // Closing quote
  parser->position++;
}

void json_scan_literal(JsonParser *parser, JsonToken *token, String literal, JsonTokenType type)
{
  String rest = {parser->text.length - parser->position, parser->text.text + parser->position};
  if (!string_starts_with(rest, literal))
  {
    json_parser_set_error(parser, JsonError_Invalid_Literal, parser->position);
    return;
  }
  token->type = type;
  token->text = (String){literal.length, rest.text};
  parser->position += literal.length;
}

// Returns a token of type None if there is an error
JsonToken json_get_token(JsonParser *parser)
{
  JsonToken token = {0};
  if (parser->error)
  {
    return token;
  }
  String text = parser->text;
  while (parser->position < text.length)
  {
    char c = text.text[parser->position];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
    {
      break;
    }
    parser->position++;
  }
  token.position = parser->position;
  if (parser->position >= text.length)
  {
    token.type = JsonTokenType_End_Of_Input;
    return token;
  }

  JsonTokenType single_character_type = JsonTokenType_None;
  char c = text.text[parser->position];
  switch (c)
  {
    case '{': { single_character_type = JsonTokenType_Open_Brace; break; }
    case '}': { single_character_type = JsonTokenType_Close_Brace; break; }
    case '[': { single_character_type = JsonTokenType_Open_Bracket; break; }
    case ']': { single_character_type = JsonTokenType_Close_Bracket; break; }
    case ',': { single_character_type = JsonTokenType_Comma; break; }
    case ':': { single_character_type = JsonTokenType_Colon; break; }
    case 't': { json_scan_literal(parser, &token, STR_LIT("true"), JsonTokenType_True); break; }
    case 'f': { json_scan_literal(parser, &token, STR_LIT("false"), JsonTokenType_False); break; }
    case 'n': { json_scan_literal(parser, &token, STR_LIT("null"), JsonTokenType_Null); break; }
    case '"': { json_scan_string(parser, &token); break; }
    default:
    {
      if (c == '-' || json_char_is_digit(c))
      {
        json_scan_number(parser, &token);
      }
      else
      {
        json_parser_set_error(parser, JsonError_Invalid_Character, parser->position);
      }
      break;
    }
  }
  if (single_character_type != JsonTokenType_None)
  {
    token.type = single_character_type;
    token.text = (String){1, text.text + parser->position};
    parser->position++;
  }
  return token;
}

void json_parser_unexpected_token(JsonParser *parser, JsonToken token)
{
  // A None token means the tokenizer has already set a more specific error
  if (token.type == JsonTokenType_End_Of_Input)
  {
    json_parser_set_error(parser, JsonError_Unexpected_End, token.position);
  }
  else if (token.type != JsonTokenType_None)
  {
    json_parser_set_error(parser, JsonError_Unexpected_Token, token.position);
  }
}

// Returns the number of bytes written (1 to 4)
i64 json_encode_utf8(u32 code_point, char *out)
{
  if (code_point < 0x80)
  {
    out[0] = (char)code_point;
    return 1;
  }
  if (code_point < 0x800)
  {
    out[0] = (char)(0xC0 | (code_point >> 6));
    out[1] = (char)(0x80 | (code_point & 0x3F));
    return 2;
  }
  if (code_point < 0x10000)
  {
    out[0] = (char)(0xE0 | (code_point >> 12));
    out[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
    out[2] = (char)(0x80 | (code_point & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (code_point >> 18));
  out[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
  out[3] = (char)(0x80 | (code_point & 0x3F));
  return 4;
}

// The 4 hex digits have been validated by json_scan_string
u32 json_parse_hex4(char *text)
{
  u32 result = 0;
  for (i32 i = 0; i < 4; ++i)
  {
    result = (result << 4) | (u32)json_hex_value(text[i]);
  }
  return result;
}

// Strings without escapes point into the source text. Others are decoded into the arena.
String json_decode_string(JsonParser *parser, JsonToken token)
{
  assert(token.type == JsonTokenType_String);
  if (!token.has_escapes)
  {
    return token.text;
  }
  // Decoding never makes a string longer: \uXXXX is at most 3 UTF-8 bytes, a surrogate pair 4
  char *in = token.text.text;
  i64 length = token.text.length;
  char *out = PushArrayNoZero(parser->arena, char, (u64)length);
  i64 out_length = 0;
  for (i64 i = 0; i < length; ++i)
  {
    if (in[i] != '\\')
    {
      out[out_length++] = in[i];
      continue;
    }
    // json_scan_string checked that a valid escape follows
    char escape = in[++i];
    switch (escape)
    {
      case 'b': { out[out_length++] = '\b'; break; }
      case 'f': { out[out_length++] = '\f'; break; }
      case 'n': { out[out_length++] = '\n'; break; }
      case 'r': { out[out_length++] = '\r'; break; }
      case 't': { out[out_length++] = '\t'; break; }
      case 'u':
      {
        i64 escape_position = token.text.text - parser->text.text + i - 1;
        u32 code_point = json_parse_hex4(in + i + 1);
        i += 4;
        if (code_point >= 0xDC00 && code_point <= 0xDFFF)
        {
          // A low surrogate without a high surrogate before it
          json_parser_set_error(parser, JsonError_Invalid_Escape, escape_position);
          return (String){0};
        }
        if (code_point >= 0xD800 && code_point <= 0xDBFF)
        {
          // A high surrogate must be followed by an escaped low surrogate
          u32 low = 0;
          if (i + 6 < length && in[i + 1] == '\\' && in[i + 2] == 'u')
          {
            low = json_parse_hex4(in + i + 3);
          }
          if (low < 0xDC00 || low > 0xDFFF)
          {
            json_parser_set_error(parser, JsonError_Invalid_Escape, escape_position);
            return (String){0};
          }
          code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
          i += 6;
        }
        out_length += json_encode_utf8(code_point, out + out_length);
        break;
      }
      default:
      {
        // '"', '\\' and '/' stand for themselves
        out[out_length++] = escape;
        break;
      }
    }
  }
  return (String){out_length, out};
}

// Element lists are built in scratch memory. Each nested object or array resets the scratch arena before it
// returns, so the elements of one list stay contiguous even though the nested values use scratch too.
JsonNode *json_push_scratch_node(JsonParser *parser, JsonNode *first, u32 count)
{
  JsonNode *node = PushArrayAligned(parser->scratch, JsonNode, 1, _Alignof(JsonNode));
  if (first && node != first + count)
  {
    InvalidCodePath;
  }
  return node;
}

b32 json_parser_enter_nesting(JsonParser *parser, JsonToken token)
{
  if (parser->depth >= JSON_MAX_DEPTH)
  {
    json_parser_set_error(parser, JsonError_Too_Deep, token.position);
    return false;
  }
  parser->depth++;
  return true;
}

void json_parse_value(JsonParser *parser, JsonToken token, JsonNode *node);

// Called after the opening brace
JsonObject json_parse_object(JsonParser *parser)
{
  JsonObject result = {0};
  TempMemory scratch_memory = temp_memory_begin(parser->scratch);
  JsonNode *members = null;
  u32 count = 0;
  JsonToken token = json_get_token(parser);
  if (token.type != JsonTokenType_Close_Brace)
  {
    while (!parser->error)
    {
      if (token.type != JsonTokenType_String)
      {
        json_parser_unexpected_token(parser, token);
        break;
      }
      JsonNode *member = json_push_scratch_node(parser, members, count);
      if (!members)
      {
        members = member;
      }
      member->name = json_decode_string(parser, token);
      member->name_hash = json_hash_string(member->name);
      token = json_get_token(parser);
      if (token.type != JsonTokenType_Colon)
      {
        json_parser_unexpected_token(parser, token);
        break;
      }
      json_parse_value(parser, json_get_token(parser), member);
      count++;
      token = json_get_token(parser);
      if (token.type == JsonTokenType_Close_Brace)
      {
        break;
      }
      if (token.type != JsonTokenType_Comma)
      {
        json_parser_unexpected_token(parser, token);
        break;
      }
      token = json_get_token(parser);
    }
  }
  if (!parser->error && count > 0)
  {
    result.length = count;
    result.values = PushArrayAlignedNoZero(parser->arena, JsonNode, count, _Alignof(JsonNode));
    memory_copy(result.values, members, count * sizeof(JsonNode));
  }
  temp_memory_end(scratch_memory);
  return result;
}

#define JSON_STORE_TYPED_ARRAY(element_type, field) \
  do \
  { \
    element_type *values = PushArrayAlignedNoZero(parser->arena, element_type, count, _Alignof(element_type)); \
    for (u32 i = 0; i < count; ++i) \
    { \
      values[i] = elements[i].field; \
    } \
    array->values = values; \
  } while (0)

void json_store_array(JsonParser *parser, JsonArray *array, JsonNode *elements, u32 count)
{
  array->length = count;
  JsonNodeType type = elements[0].type;
  for (u32 i = 1; i < count; ++i)
  {
    if (elements[i].type != type)
    {
      array->type = JsonArrayType_Mixed;
      array->values = PushArrayAlignedNoZero(parser->arena, JsonNode, count, _Alignof(JsonNode));
      memory_copy(array->values, elements, count * sizeof(JsonNode));
      return;
    }
  }
  switch (type)
  {
    case JsonNodeType_Null: { array->type = JsonArrayType_Null; break; }
    case JsonNodeType_Object:
    {
      array->type = JsonArrayType_Object;
      JSON_STORE_TYPED_ARRAY(JsonObject, object);
      break;
    }
    case JsonNodeType_Array:
    {
      array->type = JsonArrayType_Array;
      JSON_STORE_TYPED_ARRAY(JsonArray, array);
      break;
    }
    case JsonNodeType_String:
    {
      array->type = JsonArrayType_String;
      JSON_STORE_TYPED_ARRAY(String, string);
      break;
    }
    case JsonNodeType_Number:
    {
      array->type = JsonArrayType_Number;
      JSON_STORE_TYPED_ARRAY(f64, number);
      break;
    }
    case JsonNodeType_Boolean:
    {
      array->type = JsonArrayType_Boolean;
      JSON_STORE_TYPED_ARRAY(b32, boolean);
      break;
    }
    default: InvalidCodePath;
  }
}

#undef JSON_STORE_TYPED_ARRAY

// Called after the opening bracket
JsonArray json_parse_array(JsonParser *parser)
{
  JsonArray result = {0};
  TempMemory scratch_memory = temp_memory_begin(parser->scratch);
  JsonNode *elements = null;
  u32 count = 0;
  JsonToken token = json_get_token(parser);
  if (token.type != JsonTokenType_Close_Bracket)
  {
    while (!parser->error)
    {
      JsonNode *element = json_push_scratch_node(parser, elements, count);
      if (!elements)
      {
        elements = element;
      }
      json_parse_value(parser, token, element);
      count++;
      token = json_get_token(parser);
      if (token.type == JsonTokenType_Close_Bracket)
      {
        break;
      }
      if (token.type != JsonTokenType_Comma)
      {
        json_parser_unexpected_token(parser, token);
        break;
      }
      token = json_get_token(parser);
    }
  }
  if (!parser->error && count > 0)
  {
    json_store_array(parser, &result, elements, count);
  }
  temp_memory_end(scratch_memory);
  return result;
}

// Fills in everything but the name. Leaves the node as None if there is an error.
void json_parse_value(JsonParser *parser, JsonToken token, JsonNode *node)
{
  switch (token.type)
  {
    case JsonTokenType_Null: { node->type = JsonNodeType_Null; break; }
    case JsonTokenType_True:
    case JsonTokenType_False:
    {
      node->type = JsonNodeType_Boolean;
      node->boolean = token.type == JsonTokenType_True;
      break;
    }
    case JsonTokenType_Number:
    {
      if (!os_parse_f64(token.text, &node->number))
      {
        json_parser_set_error(parser, JsonError_Invalid_Number, token.position);
        break;
      }
      node->type = JsonNodeType_Number;
      break;
    }
    case JsonTokenType_String:
    {
      node->string = json_decode_string(parser, token);
      node->type = JsonNodeType_String;
      break;
    }
    case JsonTokenType_Open_Brace:
    {
      if (json_parser_enter_nesting(parser, token))
      {
        node->object = json_parse_object(parser);
        node->type = JsonNodeType_Object;
        parser->depth--;
      }
      break;
    }
    case JsonTokenType_Open_Bracket:
    {
      if (json_parser_enter_nesting(parser, token))
      {
        node->array = json_parse_array(parser);
        node->type = JsonNodeType_Array;
        parser->depth--;
      }
      break;
    }
    default:
    {
      json_parser_unexpected_token(parser, token);
      break;
    }
  }
}

// Parses any JSON value. Nodes and decoded strings go into arena, which is reset to where it was if the text
// isn't valid JSON. Safe for untrusted input: errors are reported in the document, never by crashing.
JsonDocument json_parse(Arena *arena, String text)
{
  assert(arena && (text.text || text.length == 0));
  JsonDocument document = {0};
  // Lengths and counts are u32
  if ((u64)text.length > UINT32_MAX)
  {
    document.error = JsonError_Too_Large;
    return document;
  }
  JsonParser parser = {
    .text = text,
    .arena = arena,
    .scratch = arena_get_scratch(arena),
  };
  TempMemory output_memory = temp_memory_begin(arena);
  JsonNode *root = PushArrayAligned(arena, JsonNode, 1, _Alignof(JsonNode));
  json_parse_value(&parser, json_get_token(&parser), root);
  JsonToken end = json_get_token(&parser);
  if (end.type != JsonTokenType_End_Of_Input && end.type != JsonTokenType_None)
  {
    json_parser_set_error(&parser, JsonError_Trailing_Characters, end.position);
  }
  if (parser.error)
  {
    temp_memory_end(output_memory);
    document.error = parser.error;
    document.error_position = parser.error_position;
    return document;
  }
  document.root = root;
  return document;
}

// Getters. All of them accept null and return null, 0 or false for a missing node or a different type.

JsonNodeType json_get_type(JsonNode *node)
{
  return node ? node->type : JsonNodeType_None;
}

b32 json_is_null(JsonNode *node)
{
  return node == null || node->type == JsonNodeType_Null;
}

JsonObject *json_get_object(JsonNode *node)
{
  return node && node->type == JsonNodeType_Object ? &node->object : null;
}

JsonArray *json_get_array(JsonNode *node)
{
  return node && node->type == JsonNodeType_Array ? &node->array : null;
}

// Returns the first member with this name
JsonNode *json_get_node_by_name(JsonObject *object, String name)
{
  if (!object)
  {
    return null;
  }
  u32 hash = json_hash_string(name);
  for (u32 i = 0; i < object->length; ++i)
  {
    JsonNode *member = object->values + i;
    if (member->name_hash == hash && strings_are_equal(member->name, name))
    {
      return member;
    }
  }
  return null;
}

JsonNode *json_get_node_by_index(JsonObject *object, u32 index)
{
  if (!object || index >= object->length)
  {
    return null;
  }
  return object->values + index;
}

// Returns false, and leaves result unchanged, if the node isn't a number
b32 json_get_number(JsonNode *node, f64 *result)
{
  assert(result);
  if (!node || node->type != JsonNodeType_Number)
  {
    return false;
  }
  *result = node->number;
  return true;
}

String json_get_string(JsonNode *node)
{
  return node && node->type == JsonNodeType_String ? node->string : (String){0};
}

b32 json_get_bool(JsonNode *node)
{
  return node && node->type == JsonNodeType_Boolean ? node->boolean : false;
}

JsonArrayType json_get_array_type(JsonArray *array)
{
  return array ? array->type : JsonArrayType_Empty;
}

void *json_get_typed_array(JsonArray *array, JsonArrayType type, u32 *count)
{
  assert(count);
  if (!array || array->type != type)
  {
    *count = 0;
    return null;
  }
  *count = array->length;
  return array->values;
}

b32 *json_get_bool_array(JsonArray *array, u32 *count)
{
  return json_get_typed_array(array, JsonArrayType_Boolean, count);
}

f64 *json_get_number_array(JsonArray *array, u32 *count)
{
  return json_get_typed_array(array, JsonArrayType_Number, count);
}

String *json_get_string_array(JsonArray *array, u32 *count)
{
  return json_get_typed_array(array, JsonArrayType_String, count);
}

JsonObject *json_get_object_array(JsonArray *array, u32 *count)
{
  return json_get_typed_array(array, JsonArrayType_Object, count);
}

JsonArray *json_get_array_array(JsonArray *array, u32 *count)
{
  return json_get_typed_array(array, JsonArrayType_Array, count);
}

// For arrays whose elements have different types
JsonNode *json_get_node_array(JsonArray *array, u32 *count)
{
  return json_get_typed_array(array, JsonArrayType_Mixed, count);
}
