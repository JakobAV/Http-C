
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>

#include "base_types.h"
#include "string_utils.h"
#include "http_status.h"

#include "arena.c"

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
  HttpStatus status_code;
  String content_type;
  u64 content_length;
  u32 header_count;
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
  i32 opt = 1;
  // Forcefully attaching socket to the port even if it's in TIME_WAIT
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
  {
    printf("setsockopt failed\n");
    close(server_fd);
    return -1;
  }
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
void http_parser_advance_to(HttpMessageParser *message_parser, char stop_char)
{
  while (1)
  {
    if (message_parser->current_position >= message_parser->buffer_length)
    {
      break;
    }
    char c = http_parser_get_next_character(message_parser);
    if (c == stop_char)
    {
      if (stop_char == '\r')
      {
        // skip the \n
        message_parser->current_position++;
      }
      break;
    }
  }
}

String http_parser_get_next_string(HttpMessageParser *message_parser, char stop_char)
{
  String result = {0};
  result.text = message_parser->buffer + message_parser->current_position;
  http_parser_advance_to(message_parser, stop_char);
  result.length = (message_parser->buffer + message_parser->current_position) - result.text - (stop_char == '\r' ? 2 : 1);
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
  http_parser_advance_to(message_parser, ' ');
  return result;
}

String parse_http_path(HttpMessageParser *message_parser)
{
  return http_parser_get_next_string(message_parser, ' ');
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
  http_parser_advance_to(message_parser, '\r');
  return version;
}

void parse_http_request_headers(HttpRequest *request, HttpMessageParser *message_parser)
{
  while (http_parser_peek_next_character(message_parser) != '\r')
  {
    String current_header_name = http_parser_get_next_string(message_parser, ' ');
    String current_header_value = http_parser_get_next_string(message_parser, '\r');
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
  http_parser_advance_to(message_parser, '\r');
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

i32 server_fd = -1;

Arena permanent_arena;
Arena request_arena;

void crash_handler()
{
  if (server_fd != -1)
  {
    close(server_fd);
  }
  _Exit(EXIT_FAILURE);
}

#define wrapped_send(one, two, three, four)\
printf("%.*s", (i32)three, two);\
send(one, two, three, four)

#ifndef wrapped_send
#define wrapped_send(one, two, three, four)\
send(one, two, three, four)
#endif

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
    TempMemory temp_memory = temp_memory_begin(&request_arena);
    printf("New connection\n");
    i64 total_bytes_read = 0;
    i64 max_read_size = KB(1);
    char *request_buffer = null;
    while (true)
    {
      char *p = arena_push(&request_arena, max_read_size);
      if (request_buffer == null)
      {
        request_buffer = p;
      }
      i64 bytes_read = read(new_socket_fd, p, max_read_size);
      total_bytes_read += bytes_read;
      if (bytes_read < max_read_size)
      {
        break;
      }
    }

    HttpMessageParser message_parser = {
        .buffer = request_buffer,
        .buffer_length = total_bytes_read,
        .current_position = 0,
    };
    HttpRequest request = parse_http_request(&message_parser);
    b32 supported_protocol =
        request.protocol_version == HttpProtocolVersion_0_9 ||
        request.protocol_version == HttpProtocolVersion_1_0 ||
        request.protocol_version == HttpProtocolVersion_1_1;
    HttpResponse response = {0};
    response.protocol_version = HttpProtocolVersion_1_1;
    if (!supported_protocol)
    {
      response.status_code = HttpStatus_HTTP_Version_Not_Supported;
    }
    else
    {
      if (request.method == HttpMethod_GET)
      {
        response.body = STR_LIT("<!DOCTYPE html>"
                             "<html lang=\"en\">"
                             "<head>"
                             "    <meta charset=\"UTF-8\">"
                             "    <title>200 OK</title>"
                             "</head>"
                             "<body>"
                             "    <h1>Success (200 OK)</h1>"
                             "    <p>The request was successful, and the server responded with the requested data.</p>"
                             "</body>"
                             "</html>\r\n");
        response.content_length = response.body.length;
        response.content_type = STR_LIT("text/html");
        response.status_code = HttpStatus_OK;
      }
      else if (request.method == HttpMethod_POST)
      {
        response.body = request.body;
        response.content_length = response.body.length;
        response.content_type = request.content_type;
        response.status_code = HttpStatus_OK;
      }
      else
      {
        response.status_code = HttpStatus_Method_Not_Allowed;
      }
    }
    // String response = STR_LIT("HTTP/1.1 204 OK\n\n");
    {

      // TODO: Build the response buffer and send in one call
      if (response.protocol_version == HttpProtocolVersion_1_1)
      {
        String protocol = STR_LIT("HTTP/1.1 ");
        wrapped_send(new_socket_fd, protocol.text, protocol.length, 0);
      }
      else
      {
        InvalidCodePath;
      }
      String line_break = STR_LIT("\r\n");
      String status_code = http_status_to_string(response.status_code);
      wrapped_send(new_socket_fd, status_code.text, status_code.length, 0);
      wrapped_send(new_socket_fd, line_break.text, line_break.length, 0);

      String content_type_header = STR_LIT("Content-Type: ");
      wrapped_send(new_socket_fd, content_type_header.text, content_type_header.length, 0);
      wrapped_send(new_socket_fd, response.content_type.text, response.content_type.length, 0);
      wrapped_send(new_socket_fd, line_break.text, line_break.length, 0);
      
      char temp_buffer[32] = {0};
      String content_length_string = string_from_int((i64)response.content_length, temp_buffer, sizeof(temp_buffer));
      String content_length_header = STR_LIT("Content-Length: ");
      wrapped_send(new_socket_fd, content_length_header.text, content_length_header.length, 0);
      wrapped_send(new_socket_fd, content_length_string.text, content_length_string.length, 0);
      wrapped_send(new_socket_fd, line_break.text, line_break.length, 0);
      for (u32 i = 0; i < response.header_count; ++i)
      {
        wrapped_send(new_socket_fd, response.headers[i].text, response.headers[i].length, 0);
        wrapped_send(new_socket_fd, line_break.text, line_break.length, 0);
      }
      wrapped_send(new_socket_fd, line_break.text, line_break.length, 0);
      wrapped_send(new_socket_fd, response.body.text, response.body.length, 0);
      wrapped_send(new_socket_fd, line_break.text, line_break.length, 0);
      printf("\n");
    }

    temp_memory_end(temp_memory);
  }

  printf("Done\n");
  close(server_fd);
  return 0;
}
