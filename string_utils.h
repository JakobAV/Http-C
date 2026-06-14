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
  for (i32 i = 0; i < string_one.length; ++i)
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

String string_trim_whitespace_left(String string)
{
  assert(string.text);
  String result = string;
  while (result.length > 0)
  {
    char c = result.text[0];
    b32 isWhitespace = c == ' ' || c == '\t';
    if (!isWhitespace)
    {
      break;
    }
    result.text++;
    result.length--;
  }

  return result;
}

i64 string_parse_int(String string)
{
  string = string_trim_whitespace_left(string);
  assert(string.text && string.length > 0);
  i64 result = 0;
  char c = string.text[0];
  b32 isNegative = c == '-';
  for (i32 i = isNegative ? 1 : 0; i < string.length; ++i)
  {
    c = string.text[i];
    i8 digit = (c - '0');
    assert(digit >= 0 && digit <= 9);
    result = (result * 10) + digit;
  }
  if (isNegative)
  {
    result *= -1;
  }
  return result;
}

String string_from_int(i64 number, char *buffer, u32 buffer_size)
{
  assert(buffer && buffer_size);
  String result = {0};
  if (number == 0)
  {
    buffer[0] = '0';
    result.text = buffer;
    result.length = 1;
    return result;
  }
  b32 isSigned = number < 0;
  i32 index = buffer_size - 1;

  while (number > 0 && index >= 0)
  {
    u32 remainder = number % 10;
    buffer[index--] = remainder + '0';
    number /= 10;
  }
  if (isSigned)
  {
    if (index < 0)
    {
      return result;
    }
    buffer[index] = '-';
  }
  result.text = buffer + index + 1;
  result.length = buffer_size - (index + 1);
  return result;
}