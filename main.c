#include "base_types.h"
#include "string_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>

#define DEFAULT_PORT 8888

typedef enum HttpMethod
{
  HttpMethod_NONE = 0,
  HttpMethod_GET,
  HttpMethod_HEAD,
  HttpMethod_POST,
  HttpMethod_PUT,
  HttpMethod_DELETE,
  HttpMethod_CONNECT,
  HttpMethod_OPTIONS,
  HttpMethod_TRACE,
  HttpMethod_PATCH,
} HttpMethod;

typedef enum HttpProtocolVersion
{
  HttpProtocolVersion_None = 0,
  HttpProtocolVersion_0_9,
  HttpProtocolVersion_1_0,
  HttpProtocolVersion_1_1,
  HttpProtocolVersion_2_0,
  HttpProtocolVersion_3_0,
  HttpProtocolVersion_Unknown,
} HttpProtocolVersion;

typedef struct HttpRequest
{
  String raw_message;
  HttpMethod method;
  String path;
  HttpProtocolVersion protocol_version;
  String host;
  String user_agent;
  String content_type;
  u64 content_length;
  String body;
  // TODO: Support more headers
} HttpRequest;

typedef struct HttpResponse
{
  HttpProtocolVersion protocol_version;
  u8 status_code;
  String host;
  String content_type;
  u64 content_length;
  String headers[128];
  String body;
} HttpResponse;

typedef struct HttpMessageParser
{
  char *buffer;
  u64 buffer_length;
  u64 current_position;
} HttpMessageParser;

int create_server(i16 port)
{
  i32 server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1)
  {
    printf("Error opening socket\n");
    return -1;
  }
  struct sockaddr_in socket_address = {
      .sin_port = htons(port),
      .sin_addr.s_addr = INADDR_ANY,
      .sin_family = AF_INET,
  };
  if (bind(server_fd, (struct sockaddr *)&socket_address, sizeof(struct sockaddr_in)) == -1)
  {
    printf("Error binding socket\n");
    close(server_fd);
    return -1;
  }

  if (listen(server_fd, 64) == -1)
  {
    printf("Error listening to socket\n");
    close(server_fd);
    return -1;
  }

  printf("Server is listening on port %d...", port);
  return server_fd;
}

char http_parser_peek_next_character(HttpMessageParser *message_parser)
{
  assert((message_parser->current_position) < message_parser->buffer_length);
  return message_parser->buffer[message_parser->current_position];
}

char http_parser_get_next_character(HttpMessageParser *message_parser)
{
  char c = http_parser_peek_next_character(message_parser);
  message_parser->current_position++;
  return c;
}

void http_parser_advance(HttpMessageParser *message_parser)
{
  while (1)
  {
    if (message_parser->current_position >= message_parser->buffer_length)
    {
      break;
    }
    char c = http_parser_get_next_character(message_parser);
    if (c == ' ' || c == '\n')
    {
      break;
    }
  }
}

String http_parser_get_next_string(HttpMessageParser *message_parser)
{
  String result = {0};
  result.text = message_parser->buffer + message_parser->current_position;
  http_parser_advance(message_parser);
  result.length = (message_parser->buffer + message_parser->current_position) - result.text - 1;
  if (result.text[result.length - 1] == '\r')
  {
    result.length--;
  }
  return result;
}

HttpMethod parse_http_method(HttpMessageParser *message_parser)
{
  HttpMethod result;
  char current_character = http_parser_get_next_character(message_parser);
  switch (current_character)
  {
  case 'G':
  {
    result = HttpMethod_GET;
    break;
  }
  case 'H':
  {
    result = HttpMethod_HEAD;
    break;
  }
  case 'P':
  {
    current_character = http_parser_get_next_character(message_parser);
    ;
    if (current_character == 'U')
    {
      result = HttpMethod_PUT;
      break;
    }
    if (current_character == 'O')
    {
      result = HttpMethod_POST;
      break;
    }
    if (current_character == 'A')
    {
      result = HttpMethod_PATCH;
      break;
    }
    InvalidCodePath;
  }
  case 'D':
  {
    result = HttpMethod_DELETE;
    break;
  }
  case 'C':
  {
    result = HttpMethod_CONNECT;
    break;
  }
  case 'O':
  {
    result = HttpMethod_OPTIONS;
    break;
  }
  case 'T':
  {
    result = HttpMethod_TRACE;
    break;
  }

  default:
  {
    result = HttpMethod_NONE;
    break;
  }
  break;
  }
  http_parser_advance(message_parser);
  return result;
}

String parse_http_path(HttpMessageParser *message_parser)
{
  return http_parser_get_next_string(message_parser);
}

HttpProtocolVersion parse_http_protocol_version(HttpMessageParser *message_parser)
{
  char H = http_parser_get_next_character(message_parser);
  char T1 = http_parser_get_next_character(message_parser);
  char T2 = http_parser_get_next_character(message_parser);
  char P = http_parser_get_next_character(message_parser);
  char S = http_parser_get_next_character(message_parser);
  assert(H == 'H' && T1 == 'T' && T2 == 'T' && P == 'P' && S == '/');
  HttpProtocolVersion version = HttpProtocolVersion_Unknown;
  char current_character = http_parser_get_next_character(message_parser);
  switch (current_character)
  {
  case '0':
  {
    version = HttpProtocolVersion_0_9;
    break;
  }
  case '1':
  {
    current_character = http_parser_get_next_character(message_parser);
    assert(current_character == '.');
    current_character = http_parser_get_next_character(message_parser);
    if (current_character == '0')
    {
      version = HttpProtocolVersion_1_0;
      break;
    }
    if (current_character == '1')
    {
      version = HttpProtocolVersion_1_1;
      break;
    }
    InvalidCodePath;
  }
  case '2':
  {
    version = HttpProtocolVersion_2_0;
    break;
  }
  case '3':
  {
    version = HttpProtocolVersion_3_0;
    break;
  }
  default:
    break;
  }
  http_parser_advance(message_parser);
  return version;
}

void parse_http_request_headers(HttpRequest *request, HttpMessageParser *message_parser)
{
  while (http_parser_peek_next_character(message_parser) != '\r')
  {
    String current_header_name = http_parser_get_next_string(message_parser);
    String current_header_value = http_parser_get_next_string(message_parser);
    if (strings_are_equal(current_header_name, STR_LIT("Host:")))
    {
      request->host = current_header_value;
      continue;
    }
    if (strings_are_equal(current_header_name, STR_LIT("User-Agent:")))
    {
      request->user_agent = current_header_value;
      continue;
    }
    if (strings_are_equal(current_header_name, STR_LIT("Content-Type:")))
    {
      request->content_type = current_header_value;
      continue;
    }
    if (strings_are_equal(current_header_name, STR_LIT("Content-Length:")))
    {
      request->content_length = string_parse_int(current_header_value);
      continue;
    }
    // TODO: Add support for more and none-special-case headers
  }
  http_parser_advance(message_parser);
}

HttpRequest parse_http_request(HttpMessageParser *message_parser)
{
  assert(message_parser->buffer && message_parser->buffer_length > 0);
  HttpRequest request = {0};

  request.method = parse_http_method(message_parser);
  request.path = parse_http_path(message_parser);
  request.protocol_version = parse_http_protocol_version(message_parser);
  parse_http_request_headers(&request, message_parser);

  if (request.content_length > 0)
  {
    request.body.text = message_parser->buffer + message_parser->current_position;
    request.body.length = message_parser->buffer_length - message_parser->current_position;
    assert((u64)request.body.length == request.content_length);
  }
  return request;
}

char buffer[KB(1)];
i32 server_fd = -1;

void crash_handler()
{
  if (server_fd != -1)
  {
    close(server_fd);
  }
  _Exit(EXIT_FAILURE);
}

int main()
{
  signal(SIGSEGV, crash_handler);
  signal(SIGFPE, crash_handler);
  signal(SIGABRT, crash_handler);
  server_fd = create_server(DEFAULT_PORT);
  if (server_fd == -1)
  {
    printf("Failed to create server\n");
    return -1;
  }

  u32 address_length = sizeof(struct sockaddr_in);
  while (1)
  {
    struct sockaddr_in connection_address = {0};
    i32 new_socket_fd = accept(server_fd, (struct sockaddr *)&connection_address, (socklen_t *)&address_length);
    if (new_socket_fd < 0)
    {
      printf("Failed to establish connection. Error code: %d\n", new_socket_fd);
      continue;
    }

    printf("New connection\n");
    i64 bytes_read = 0;
    i64 max_read_size = sizeof(buffer) - 1;
    HttpRequest out = {0};
    do
    {
      bytes_read = read(new_socket_fd, buffer, max_read_size);
      if (bytes_read > 0)
      {
        HttpMessageParser message_parser = {
            .buffer = buffer,
            .buffer_length = bytes_read,
            .current_position = 0,
        };
        HttpRequest request = parse_http_request(&message_parser);
        request.raw_message.text = buffer;
        request.raw_message.length = bytes_read;
        out = request;
      }
    } while (bytes_read == max_read_size);
    printf("\nHeader:\n%.*s\n", (i32)(out.body.text-out.raw_message.text), out.raw_message.text);
    printf("\nBody:\n%.*s\n", (i32)out.body.length, out.body.text);
    String response = STR_LIT("HTTP/1.1 204 OK\n\n");
    send(new_socket_fd, response.text, response.length, 0);
  }

  printf("Done\n");
  close(server_fd);
  return 0;
}
