// Unity build for the tests, built and run by build_tests.sh
#if defined(__linux__)
#include "../os_linux.c"
#else
#error "Unsupported platform"
#endif

#include "../base_types.h"

#include "../string_utils.c"
#include "../memory_utils.c"
#include "../arena.c"
#include "../json.c"

// A large real-world document. It isn't part of the repository, so the test is skipped if it's missing.
#ifndef TEST_JSON_PATH
#define TEST_JSON_PATH "/home/yago/Projects/ScrachPad/ScratchPad/Test.json"
#endif

u32 checks_run;
u32 checks_failed;

#define Check(condition) test_check((condition), #condition, __LINE__)

void test_check(b32 passed, char *expression, i32 line)
{
  checks_run++;
  if (!passed)
  {
    checks_failed++;
    os_print("FAILED (line %d): %s\n", line, expression);
  }
}

Arena test_arena;

JsonDocument parse(String text)
{
  return json_parse(&test_arena, text);
}

// Parses text that must be valid and returns its root
JsonNode *parse_valid(String text, i32 line)
{
  JsonDocument document = parse(text);
  if (document.error)
  {
    String error = json_error_to_string(document.error);
    os_print("FAILED (line %d): unexpected error \"%.*s\" at %d in %.*s\n", line, (i32)error.length, error.text,
             (i32)document.error_position, (i32)text.length, text.text);
  }
  test_check(document.error == JsonError_None && document.root != null, "parse_valid", line);
  return document.root;
}

void check_error(String text, JsonError expected, i32 line)
{
  JsonDocument document = parse(text);
  b32 passed = document.error == expected && document.root == null;
  if (!passed)
  {
    String got = json_error_to_string(document.error);
    String want = json_error_to_string(expected);
    os_print("FAILED (line %d): %.*s gave \"%.*s\", expected \"%.*s\"\n", line, (i32)text.length, text.text,
             (i32)got.length, got.text, (i32)want.length, want.text);
  }
  test_check(passed, "check_error", line);
}

#define ParseValid(literal) parse_valid(STR_LIT(literal), __LINE__)
#define CheckError(literal, error) check_error(STR_LIT(literal), JsonError_##error, __LINE__)

b32 number_is(JsonNode *node, f64 expected)
{
  f64 value = 0;
  return json_get_number(node, &value) && value == expected;
}

b32 string_is(JsonNode *node, String expected)
{
  String value = json_get_string(node);
  return value.text && strings_are_equal(value, expected);
}

void test_scalars(void)
{
  Check(json_get_type(ParseValid("null")) == JsonNodeType_Null);
  Check(json_get_bool(ParseValid("true")) == true);
  Check(json_get_type(ParseValid(" false ")) == JsonNodeType_Boolean);
  Check(number_is(ParseValid("42"), 42));
  Check(number_is(ParseValid("-0"), 0));
  Check(number_is(ParseValid("0.5"), 0.5));
  Check(number_is(ParseValid("0.1"), 0.1));
  Check(number_is(ParseValid("1e10"), 1e10));
  Check(number_is(ParseValid("-1.5E-3"), -1.5E-3));
  Check(number_is(ParseValid("2e+2"), 200));
  Check(number_is(ParseValid("123456789012"), 123456789012.0));
  Check(number_is(ParseValid("1e-400"), 0));
  Check(string_is(ParseValid("\"hello\""), STR_LIT("hello")));
  Check(string_is(ParseValid("\"\""), STR_LIT("")));
}

void test_strings(void)
{
  // Strings without escapes point into the source text
  String text = STR_LIT("\"plain\"");
  JsonDocument document = parse(text);
  Check(json_get_string(document.root).text == text.text + 1);

  Check(string_is(ParseValid("\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\""), STR_LIT("a\"b\\c/d\b\f\n\r\t")));
  Check(string_is(ParseValid("\"\\\\\""), STR_LIT("\\")));
  Check(string_is(ParseValid("\"\\u0041\""), STR_LIT("A")));
  Check(string_is(ParseValid("\"\\u00e9\""), STR_LIT("\xC3\xA9")));
  Check(string_is(ParseValid("\"\\u20AC\""), STR_LIT("\xE2\x82\xAC")));
  Check(string_is(ParseValid("\"\\ud83d\\ude00\""), STR_LIT("\xF0\x9F\x98\x80")));
  Check(string_is(ParseValid("\"x\\u0000y\""), STR_LIT("x\0y")));
  // UTF-8 is passed through unchanged
  Check(string_is(ParseValid("\"\xC3\xA9\""), STR_LIT("\xC3\xA9")));
}

void test_objects(void)
{
  JsonNode *root = ParseValid(" { \"a\" : 1 , \"b\":\"x\",\"c\":true,\"d\":null,\"e\":[1,2,3],\"f\":{\"g\":false},"
                              "\"\":7, \"a\":2, \"esc\\naped\":3 } ");
  JsonObject *object = json_get_object(root);
  Check(object && object->length == 9);
  Check(number_is(json_get_node_by_name(object, STR_LIT("a")), 1)); // The first of duplicate names wins
  Check(string_is(json_get_node_by_name(object, STR_LIT("b")), STR_LIT("x")));
  Check(json_get_bool(json_get_node_by_name(object, STR_LIT("c"))) == true);
  Check(json_get_type(json_get_node_by_name(object, STR_LIT("d"))) == JsonNodeType_Null);
  Check(json_is_null(json_get_node_by_name(object, STR_LIT("d"))));
  Check(json_is_null(json_get_node_by_name(object, STR_LIT("missing"))));
  Check(json_get_node_by_name(object, STR_LIT("missing")) == null);
  Check(number_is(json_get_node_by_name(object, STR_LIT("")), 7));
  Check(number_is(json_get_node_by_name(object, STR_LIT("esc\naped")), 3));
  JsonObject *inner = json_get_object(json_get_node_by_name(object, STR_LIT("f")));
  Check(inner && inner->length == 1);
  Check(json_get_type(json_get_node_by_name(inner, STR_LIT("g"))) == JsonNodeType_Boolean);
  Check(number_is(json_get_node_by_index(object, 7), 2));
  Check(json_get_node_by_index(object, 9) == null);

  u32 count = 0;
  f64 *numbers = json_get_number_array(json_get_array(json_get_node_by_name(object, STR_LIT("e"))), &count);
  Check(numbers && count == 3 && numbers[0] == 1 && numbers[2] == 3);

  // Getters with the wrong type
  f64 number = 5;
  Check(!json_get_number(json_get_node_by_name(object, STR_LIT("b")), &number) && number == 5);
  Check(json_get_string(json_get_node_by_name(object, STR_LIT("a"))).text == null);
  Check(json_get_object(json_get_node_by_name(object, STR_LIT("a"))) == null);
  Check(json_get_string_array(json_get_array(json_get_node_by_name(object, STR_LIT("e"))), &count) == null);
  Check(count == 0);
  Check(json_get_node_by_name(null, STR_LIT("a")) == null);

  JsonObject *empty = json_get_object(ParseValid("{}"));
  Check(empty && empty->length == 0 && json_get_node_by_index(empty, 0) == null);
}

void test_arrays(void)
{
  u32 count = 0;
  JsonArray *empty = json_get_array(ParseValid("[ ]"));
  Check(empty && json_get_array_type(empty) == JsonArrayType_Empty && empty->length == 0);

  JsonArray *nulls = json_get_array(ParseValid("[null, null]"));
  Check(nulls && json_get_array_type(nulls) == JsonArrayType_Null && nulls->length == 2);

  b32 *bools = json_get_bool_array(json_get_array(ParseValid("[true,false,true]")), &count);
  Check(bools && count == 3 && bools[0] && !bools[1] && bools[2]);

  String *strings = json_get_string_array(json_get_array(ParseValid("[\"a\",\"b\\tc\"]")), &count);
  Check(strings && count == 2 && strings_are_equal(strings[1], STR_LIT("b\tc")));

  JsonObject *objects = json_get_object_array(json_get_array(ParseValid("[{\"a\":1},{\"a\":2,\"b\":3}]")), &count);
  Check(objects && count == 2 && objects[1].length == 2);
  Check(number_is(json_get_node_by_name(&objects[1], STR_LIT("a")), 2));

  JsonArray *arrays = json_get_array_array(json_get_array(ParseValid("[[1,2],[\"x\"],[]]")), &count);
  Check(arrays && count == 3);
  if (arrays)
  {
    u32 inner_count = 0;
    f64 *inner = json_get_number_array(&arrays[0], &inner_count);
    Check(inner && inner_count == 2 && inner[1] == 2);
    Check(json_get_string_array(&arrays[1], &inner_count) && inner_count == 1);
    Check(json_get_array_type(&arrays[2]) == JsonArrayType_Empty);
  }

  // Elements of different types, which crashed the original parser
  JsonArray *mixed = json_get_array(ParseValid("[1,\"a\",null,true,{\"k\":[null]},[2]]"));
  Check(mixed && json_get_array_type(mixed) == JsonArrayType_Mixed);
  JsonNode *nodes = json_get_node_array(mixed, &count);
  Check(nodes && count == 6);
  if (nodes)
  {
    Check(number_is(&nodes[0], 1));
    Check(string_is(&nodes[1], STR_LIT("a")));
    Check(json_is_null(&nodes[2]));
    Check(json_get_bool(&nodes[3]));
    JsonArray *inner = json_get_array(json_get_node_by_name(json_get_object(&nodes[4]), STR_LIT("k")));
    Check(json_get_array_type(inner) == JsonArrayType_Null);
    Check(json_get_type(&nodes[5]) == JsonNodeType_Array);
  }
  JsonArray *starts_with_null = json_get_array(ParseValid("[null, 1]"));
  Check(json_get_array_type(starts_with_null) == JsonArrayType_Mixed);
}

void test_nesting(void)
{
  char buffer[1024];
  // JSON_MAX_DEPTH levels are fine, one more is an error
  for (u32 depth = JSON_MAX_DEPTH; depth <= JSON_MAX_DEPTH + 1; ++depth)
  {
    for (u32 i = 0; i < depth; ++i)
    {
      buffer[i] = '[';
      buffer[depth + i] = ']';
    }
    JsonDocument document = parse((String){depth * 2, buffer});
    Check(document.error == (depth == JSON_MAX_DEPTH ? JsonError_None : JsonError_Too_Deep));
  }
  // Deep enough to overflow the stack without the limit
  u64 deep_length = MB(1);
  char *deep = PushArrayNoZero(&test_arena, char, deep_length);
  memory_set(deep, '[', deep_length);
  Check(parse((String){deep_length, deep}).error == JsonError_Too_Deep);
  memory_set(deep, '{', deep_length);
  Check(parse((String){deep_length, deep}).error == JsonError_Unexpected_Token);
  for (u64 i = 0; i < deep_length; i += 5)
  {
    memory_copy(deep + i, "{\"a\":", 5);
  }
  Check(parse((String){deep_length, deep}).error == JsonError_Too_Deep);
}

void test_malformed(void)
{
  CheckError("", Unexpected_End);
  CheckError("   ", Unexpected_End);
  CheckError("{", Unexpected_End);
  CheckError("[", Unexpected_End);
  CheckError("[1,", Unexpected_End);
  CheckError("{\"a\"", Unexpected_End);
  CheckError("{\"a\":", Unexpected_End);
  CheckError("{\"a\":1", Unexpected_End);
  CheckError("[1,]", Unexpected_Token);
  CheckError("[,1]", Unexpected_Token);
  CheckError("{\"a\":1,}", Unexpected_Token);
  CheckError("{,}", Unexpected_Token);
  CheckError("{1:2}", Unexpected_Token);
  CheckError("{\"a\" 1}", Unexpected_Token);
  CheckError("[1 2]", Unexpected_Token);
  CheckError("]", Unexpected_Token);
  CheckError("1 2", Trailing_Characters);
  CheckError("{} x", Invalid_Character);
  CheckError("{}}", Trailing_Characters);
  CheckError("tru", Invalid_Literal);
  CheckError("nul", Invalid_Literal);
  CheckError("True", Invalid_Character);
  CheckError("truex", Invalid_Character);
  CheckError("01", Invalid_Number);
  CheckError("-01", Invalid_Number);
  CheckError("1.", Invalid_Number);
  CheckError("1.e5", Invalid_Number);
  CheckError(".5", Invalid_Character);
  CheckError("-", Invalid_Number);
  CheckError("-a", Invalid_Number);
  CheckError("+1", Invalid_Character);
  CheckError("1e", Invalid_Number);
  CheckError("1e+", Invalid_Number);
  CheckError("1e999", Invalid_Number);
  CheckError("0x10", Invalid_Character);
  CheckError("\"abc", Invalid_String);
  CheckError("\"abc\\", Invalid_String);
  CheckError("\"abc\\\"", Invalid_String);
  CheckError("\"a\nb\"", Invalid_String);
  CheckError("\"a\tb\"", Invalid_String);
  CheckError("\"\\x\"", Invalid_Escape);
  CheckError("\"\\u12\"", Invalid_Escape);
  CheckError("\"\\u12", Invalid_Escape);
  CheckError("\"\\uZZZZ\"", Invalid_Escape);
  CheckError("\"\\ud83d\"", Invalid_Escape);
  CheckError("\"\\ud83dx\"", Invalid_Escape);
  CheckError("\"\\ud83d\\u0041\"", Invalid_Escape);
  CheckError("\"\\ude00\"", Invalid_Escape);
  CheckError("{\"\\ude00\":1}", Invalid_Escape);
  CheckError("[1,\"\\x\"]", Invalid_Escape);

  JsonDocument document = parse(STR_LIT("[1, 2, x]"));
  Check(document.error == JsonError_Invalid_Character && document.error_position == 7);

  // Every truncation of a valid document is an error
  String valid = STR_LIT("{\"a\": [1, -2.5e3, \"s\\u00e9\", true, false, null, {\"b\": {}}], \"c\": \"d\"}");
  Check(parse(valid).error == JsonError_None);
  for (i64 length = 0; length < valid.length; ++length)
  {
    Check(parse((String){length, valid.text}).error != JsonError_None);
  }
}

// Mutates a valid document at random. The parser must never crash or assert, whatever the result.
void test_fuzz(void)
{
  String valid = STR_LIT("{\"a\": [1, -2.5e3, \"s\\u00e9\\ud83d\\ude00\", true, false, null, {\"b\": {}}],"
                         " \"c\": \"d\", \"e\": [[1], [\"x\"], []]}");
  char characters[] = "{}[]:,\"\\u0123456789abcdefABCDEF.eE+-tnlrsx \n\x01\xff";
  char buffer[256];
  u64 random = 0x9E3779B97F4A7C15;
  u32 parsed_valid = 0;
  for (u32 iteration = 0; iteration < 200000; ++iteration)
  {
    memory_copy(buffer, valid.text, valid.length);
    i64 length = valid.length;
    u32 mutations = 1 + iteration % 4;
    for (u32 m = 0; m < mutations; ++m)
    {
      // xorshift64
      random ^= random << 13;
      random ^= random >> 7;
      random ^= random << 17;
      u64 position = (random >> 8) % (u64)length;
      char replacement = characters[(random >> 40) % (ArrayCount(characters) - 1)];
      if ((random & 3) == 0 && length > 1)
      {
        // Delete a character
        memory_copy(buffer + position, buffer + position + 1, length - position - 1);
        length--;
      }
      else
      {
        buffer[position] = replacement;
      }
    }
    TempMemory temp_memory = temp_memory_begin(&test_arena);
    JsonDocument document = parse((String){length, buffer});
    parsed_valid += document.error == JsonError_None;
    Check(document.error ? document.root == null : document.root != null);
    temp_memory_end(temp_memory);
  }
  os_print("  fuzz: %u of 200000 mutated documents were still valid\n", parsed_valid);
}

void test_scratch_output(void)
{
  // The output arena can itself be a scratch arena: the parser must then use the other one
  for (u32 i = 0; i < 2; ++i)
  {
    Arena *scratch = arena_get_scratch(i == 0 ? null : arena_get_scratch(null));
    TempMemory temp_memory = temp_memory_begin(scratch);
    JsonDocument document = json_parse(scratch, STR_LIT("{\"a\":[[1,2],{\"b\":\"\\u00e9\"}],\"c\":[3]}"));
    Check(document.error == JsonError_None);
    JsonObject *root = json_get_object(document.root);
    u32 count = 0;
    f64 *c = json_get_number_array(json_get_array(json_get_node_by_name(root, STR_LIT("c"))), &count);
    Check(c && count == 1 && c[0] == 3);
    temp_memory_end(temp_memory);
  }
}

void test_large_file(void)
{
  String text = {0};
  if (!os_read_entire_file(&test_arena, STR_LIT(TEST_JSON_PATH), &text))
  {
    os_print("  skipped: couldn't read %s\n", TEST_JSON_PATH);
    return;
  }
  JsonDocument document = parse(text);
  Check(document.error == JsonError_None);
  JsonObject *root = json_get_object(document.root);
  u32 count = 0;
  f64 *untagged = json_get_number_array(json_get_array(json_get_node_by_name(root, STR_LIT("Untagged"))), &count);
  Check(untagged && count == 3 && untagged[2] == 3);
  os_print("  parsed %d bytes, %u root members\n", (i32)text.length, root ? root->length : 0);
}

int main(void)
{
  struct
  {
    char *name;
    void (*run)(void);
  } tests[] = {
    {"scalars", test_scalars},
    {"strings", test_strings},
    {"objects", test_objects},
    {"arrays", test_arrays},
    {"nesting", test_nesting},
    {"malformed", test_malformed},
    {"fuzz", test_fuzz},
    {"scratch output", test_scratch_output},
    {"large file", test_large_file},
  };
  for (u32 i = 0; i < ArrayCount(tests); ++i)
  {
    os_print("%s\n", tests[i].name);
    TempMemory temp_memory = temp_memory_begin(&test_arena);
    tests[i].run();
    temp_memory_end(temp_memory);
  }
  os_print("%u checks, %u failed\n", checks_run, checks_failed);
  return checks_failed == 0 ? 0 : 1;
}
