#pragma once
#include "base_types.h"

b32 strings_are_equal(String string_one, String string_two)
{
  assert(string_one.text && string_two.text);
  if (string_one.length != string_two.length)
  {
    return false;
  }
  if (string_one.text == string_two.text)
  {
    // The strings point to the same text with the same length. They are the same.
    return true;
  }
  for (i64 i = 0; i < string_one.length; ++i)
  {
    char one = string_one.text[i];
    char two = string_two.text[i];
    if (one != two)
    {
      return false;
    }
  }
  return true;
}

b32 string_starts_with(String string, String prefix)
{
  if (string.length < prefix.length)
  {
    return false;
  }
  return strings_are_equal((String){prefix.length, string.text}, prefix);
}

// Returns the index of the first occurrence of needle, or -1 if there is none.
i64 string_find(String haystack, String needle)
{
  for (i64 i = 0; i + needle.length <= haystack.length; ++i)
  {
    if (strings_are_equal((String){needle.length, haystack.text + i}, needle))
    {
      return i;
    }
  }
  return -1;
}

b32 char_is_whitespace(char c)
{
  return c == ' ' || c == '\t';
}

String string_trim_whitespace_left(String string)
{
  assert(string.text);
  String result = string;
  while (result.length > 0 && char_is_whitespace(result.text[0]))
  {
    result.text++;
    result.length--;
  }
  return result;
}

String string_trim_whitespace_right(String string)
{
  assert(string.text);
  String result = string;
  while (result.length > 0 && char_is_whitespace(result.text[result.length - 1]))
  {
    result.length--;
  }
  return result;
}

String string_trim_whitespace(String string)
{
  return string_trim_whitespace_right(string_trim_whitespace_left(string));
}

// Returns false if the string is not a valid integer or doesn't fit in an i64.
b32 string_parse_int(String string, i64 *result)
{
  assert(result);
  string = string_trim_whitespace_left(string);
  b32 is_negative = string.length > 0 && string.text[0] == '-';
  i64 start = is_negative ? 1 : 0;
  if (string.length <= start)
  {
    return false;
  }
  u64 limit = is_negative ? (u64)INT64_MAX + 1 : (u64)INT64_MAX;
  u64 magnitude = 0;
  for (i64 i = start; i < string.length; ++i)
  {
    char c = string.text[i];
    if (c < '0' || c > '9')
    {
      return false;
    }
    u64 digit = c - '0';
    if (magnitude > (limit - digit) / 10)
    {
      return false;
    }
    magnitude = (magnitude * 10) + digit;
  }
  if (is_negative && magnitude > 0)
  {
    // Negate via magnitude - 1 so INT64_MIN doesn't overflow
    *result = -(i64)(magnitude - 1) - 1;
  }
  else
  {
    *result = (i64)magnitude;
  }
  return true;
}

// Writes the number to the end of buffer. Returns an empty String if the buffer is too small.
String string_from_int(i64 number, char *buffer, u32 buffer_size)
{
  assert(buffer && buffer_size);
  String result = {0};
  b32 is_negative = number < 0;
  u64 magnitude = is_negative ? 0 - (u64)number : (u64)number;
  i64 index = buffer_size;
  do
  {
    if (index == 0)
    {
      return result;
    }
    buffer[--index] = '0' + (magnitude % 10);
    magnitude /= 10;
  } while (magnitude > 0);
  if (is_negative)
  {
    if (index == 0)
    {
      return result;
    }
    buffer[--index] = '-';
  }
  result.text = buffer + index;
  result.length = buffer_size - index;
  return result;
}
