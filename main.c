// Must be the first include: os_linux.c sets feature-test macros before any system header
#if defined(__linux__)
#include "os_linux.c"
#else
#error "Unsupported platform"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>

#include "base_types.h"

#include "string_utils.c"
#include "memory_utils.c"
#include "http_status.c"
#include "arena.c"
#include "json.c"
#include "work_queue.c"

#define DEFAULT_PORT 8888
#define READ_CHUNK_SIZE KB(4)
#define MAX_REQUEST_SIZE MB(1)

#define HTTP_METHOD_LIST \
  X(GET) \
  X(HEAD) \
  X(POST) \
  X(PUT) \
  X(DELETE) \
  X(CONNECT) \
  X(OPTIONS) \
  X(TRACE) \
  X(PATCH)

typedef enum HttpMethod
{
  HttpMethod_NONE = 0,
#define X(name) HttpMethod_##name,
  HTTP_METHOD_LIST
#undef X
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
  HttpStatus error;
} HttpMessageParser;

// Client input we don't handle (yet). Asserts in debug builds so the case surfaces during development,
// otherwise records the status to respond with. The first error wins.
#define UnhandledInput(parser, status) \
  do \
  { \
    assert(!"Unhandled input"); \
    if (!(parser)->error) \
    { \
      (parser)->error = (status); \
    } \
  } while (0)

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
  if (message_parser->current_position >= message_parser->buffer_length)
  {
    UnhandledInput(message_parser, HttpStatus_Bad_Request);
    return 0;
  }
  return message_parser->buffer[message_parser->current_position];
}

char http_parser_get_next_character(HttpMessageParser *message_parser)
{
  char c = http_parser_peek_next_character(message_parser);
  if (message_parser->current_position < message_parser->buffer_length)
  {
    message_parser->current_position++;
  }
  return c;
}

// Returns the text up to stop_char and moves past it. A '\r' stop_char also consumes the following '\n'.
String http_parser_get_next_string(HttpMessageParser *message_parser, char stop_char)
{
  String result = {0};
  result.text = message_parser->buffer + message_parser->current_position;
  while (message_parser->current_position < message_parser->buffer_length &&
         message_parser->buffer[message_parser->current_position] != stop_char)
  {
    message_parser->current_position++;
  }
  result.length = (message_parser->buffer + message_parser->current_position) - result.text;
  // Consume stop_char, flags an error if it was missing
  http_parser_get_next_character(message_parser);
  if (stop_char == '\r' && http_parser_get_next_character(message_parser) != '\n')
  {
    UnhandledInput(message_parser, HttpStatus_Bad_Request);
  }
  return result;
}

HttpMethod parse_http_method(HttpMessageParser *message_parser)
{
  String method = http_parser_get_next_string(message_parser, ' ');
  if (message_parser->error)
  {
    return HttpMethod_NONE;
  }
#define X(name) \
  if (strings_are_equal(method, STR_LIT(#name))) \
  { \
    return HttpMethod_##name; \
  }
  HTTP_METHOD_LIST
#undef X
  UnhandledInput(message_parser, HttpStatus_Not_Implemented);
  return HttpMethod_NONE;
}

String parse_http_path(HttpMessageParser *message_parser)
{
  return http_parser_get_next_string(message_parser, ' ');
}

HttpProtocolVersion parse_http_protocol_version(HttpMessageParser *message_parser)
{
  String version = http_parser_get_next_string(message_parser, '\r');
  if (message_parser->error)
  {
    return HttpProtocolVersion_None;
  }
  if (!string_starts_with(version, STR_LIT("HTTP/")))
  {
    UnhandledInput(message_parser, HttpStatus_Bad_Request);
    return HttpProtocolVersion_None;
  }
  if (strings_are_equal(version, STR_LIT("HTTP/1.0")))
  {
    return HttpProtocolVersion_1_0;
  }
  if (strings_are_equal(version, STR_LIT("HTTP/1.1")))
  {
    return HttpProtocolVersion_1_1;
  }
  // TODO: Support HTTP/2 and HTTP/3
  UnhandledInput(message_parser, HttpStatus_HTTP_Version_Not_Supported);
  return HttpProtocolVersion_Unknown;
}

void parse_http_request_headers(HttpRequest *request, HttpMessageParser *message_parser)
{
  while (!message_parser->error && http_parser_peek_next_character(message_parser) != '\r')
  {
    String current_header_name = http_parser_get_next_string(message_parser, ':');
    String current_header_value = string_trim_whitespace(http_parser_get_next_string(message_parser, '\r'));
    if (strings_are_equal_ignore_case(current_header_name, STR_LIT("Host")))
    {
      request->host = current_header_value;
      continue;
    }
    if (strings_are_equal_ignore_case(current_header_name, STR_LIT("User-Agent")))
    {
      request->user_agent = current_header_value;
      continue;
    }
    if (strings_are_equal_ignore_case(current_header_name, STR_LIT("Content-Type")))
    {
      request->content_type = current_header_value;
      continue;
    }
    if (strings_are_equal_ignore_case(current_header_name, STR_LIT("Content-Length")))
    {
      i64 content_length = 0;
      if (!string_parse_int(current_header_value, &content_length) || content_length < 0)
      {
        UnhandledInput(message_parser, HttpStatus_Bad_Request);
        continue;
      }
      request->content_length = content_length;
      continue;
    }
    // TODO: Add support for more and none-special-case headers
  }
  // Consume the empty line that ends the headers
  http_parser_get_next_string(message_parser, '\r');
}

// Parses the request line and headers. The body is filled in by read_http_request.
HttpRequest parse_http_request(HttpMessageParser *message_parser)
{
  assert(message_parser->buffer && message_parser->buffer_length > 0);
  HttpRequest request = {0};

  request.method = parse_http_method(message_parser);
  request.path = parse_http_path(message_parser);
  request.protocol_version = parse_http_protocol_version(message_parser);
  parse_http_request_headers(&request, message_parser);
  return request;
}

// Reads the headers and Content-Length bytes of body into the arena, then parses the request.
// Returns false if the connection failed and no response can be sent. Errors in the request
// itself are reported through message_parser->error.
b32 read_http_request(i32 socket_fd, Arena *arena, HttpMessageParser *message_parser, HttpRequest *request)
{
  char *buffer = null;
  u64 capacity = 0;
  u64 length = 0;
  // Offset just past the "\r\n\r\n" that ends the headers, 0 until it has been read
  u64 head_length = 0;
  u64 request_length = 0;
  while (head_length == 0 || length < request_length)
  {
    if (length == capacity)
    {
      if (capacity >= MAX_REQUEST_SIZE)
      {
        UnhandledInput(message_parser, HttpStatus_Request_Header_Fields_Too_Large);
        return true;
      }
      char *chunk = arena_push_no_zero(arena, READ_CHUNK_SIZE);
      if (buffer == null)
      {
        buffer = chunk;
      }
      // The buffer grows through consecutive pushes, so nothing else may push to the arena while reading
      if (chunk != buffer + capacity)
      {
        InvalidCodePath;
      }
      capacity += READ_CHUNK_SIZE;
    }
    i64 bytes_read = read(socket_fd, buffer + length, capacity - length);
    if (bytes_read < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      printf("Failed to read from socket. errno: %d\n", errno);
      return false;
    }
    if (bytes_read == 0)
    {
      // The client closed the connection before sending a complete request
      return false;
    }
    // The terminator may straddle the previous read
    u64 search_start = length >= 3 ? length - 3 : 0;
    length += bytes_read;
    if (head_length == 0)
    {
      i64 terminator = string_find((String){length - search_start, buffer + search_start}, STR_LIT("\r\n\r\n"));
      if (terminator >= 0)
      {
        head_length = search_start + terminator + 4;
        message_parser->buffer = buffer;
        message_parser->buffer_length = head_length;
        *request = parse_http_request(message_parser);
        if (message_parser->error)
        {
          return true;
        }
        if (request->content_length > MAX_REQUEST_SIZE - head_length)
        {
          UnhandledInput(message_parser, HttpStatus_Payload_Too_Large);
          return true;
        }
        request_length = head_length + request->content_length;
      }
    }
  }
  request->raw_message = (String){request_length, buffer};
  request->body = (String){request->content_length, buffer + head_length};
  return true;
}

// MSG_NOSIGNAL: a client that disconnects early must not kill the server with SIGPIPE.
// With several worker threads, the DEBUG_LOG echo of different responses can interleave. Each print is
// thread-safe, so that's acceptable for debug output.
b32 send_string(i32 socket_fd, String string)
{
#ifdef DEBUG_LOG
  printf("%.*s", (i32)string.length, string.text);
#endif
  while (string.length > 0)
  {
    i64 bytes_sent = send(socket_fd, string.text, string.length, MSG_NOSIGNAL);
    if (bytes_sent < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      return false;
    }
    string.text += bytes_sent;
    string.length -= bytes_sent;
  }
  return true;
}

// Compares the media type of a Content-Type value, ignoring parameters such as "; charset=utf-8"
b32 http_content_type_is(String content_type, String media_type)
{
  i64 end = 0;
  while (end < content_type.length && content_type.text[end] != ';')
  {
    end++;
  }
  String type = string_trim_whitespace((String){end, content_type.text});
  return strings_are_equal_ignore_case(type, media_type);
}

void send_http_response(i32 socket_fd, HttpResponse *response)
{
  // TODO: Build the response buffer and send in one call
  // Failed sends are ignored: once the connection is broken the remaining sends fail too, and it's closed after.
  if (response->protocol_version != HttpProtocolVersion_1_1)
  {
    InvalidCodePath;
  }
  String line_break = STR_LIT("\r\n");
  send_string(socket_fd, STR_LIT("HTTP/1.1 "));
  send_string(socket_fd, http_status_to_string(response->status_code));
  send_string(socket_fd, line_break);

  if (response->content_type.length > 0)
  {
    send_string(socket_fd, STR_LIT("Content-Type: "));
    send_string(socket_fd, response->content_type);
    send_string(socket_fd, line_break);
  }

  char temp_buffer[32] = {0};
  send_string(socket_fd, STR_LIT("Content-Length: "));
  send_string(socket_fd, string_from_int((i64)response->content_length, temp_buffer, sizeof(temp_buffer)));
  send_string(socket_fd, line_break);
  for (u32 i = 0; i < response->header_count; ++i)
  {
    send_string(socket_fd, response->headers[i]);
    send_string(socket_fd, line_break);
  }
  send_string(socket_fd, line_break);
  send_string(socket_fd, response->body);
#ifdef DEBUG_LOG
  printf("\n");
#endif
}

i32 server_fd = -1;

Arena permanent_arena;
// Each worker thread handles one connection at a time and resets its arena after it
ThreadLocal Arena request_arena;

WorkQueue connection_queue;

void crash_handler()
{
  if (server_fd != -1)
  {
    close(server_fd);
  }
  _Exit(EXIT_FAILURE);
}

HttpResponse handle_http_request(HttpMessageParser *message_parser, HttpRequest *request)
{
  HttpResponse response = {0};
  response.protocol_version = HttpProtocolVersion_1_1;
  if (message_parser->error)
  {
    response.status_code = message_parser->error;
  }
  else if (request->method == HttpMethod_GET)
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
  else if (request->method == HttpMethod_POST)
  {
    b32 valid_body = true;
    if (http_content_type_is(request->content_type, STR_LIT("application/json")))
    {
      JsonDocument document = json_parse(&request_arena, request->body);
      valid_body = document.error == JsonError_None;
    }
    if (valid_body)
    {
      response.body = request->body;
      response.content_length = response.body.length;
      response.content_type = request->content_type;
      response.status_code = HttpStatus_OK;
    }
    else
    {
      response.status_code = HttpStatus_Bad_Request;
    }
  }
  else
  {
    response.status_code = HttpStatus_Method_Not_Allowed;
  }
  return response;
}

// Runs on a worker thread. data is the connection's socket.
void handle_connection(WorkQueue *queue, void *data)
{
  (void)queue;
  i32 socket_fd = (i32)(uintptr_t)data;
  TempMemory temp_memory = temp_memory_begin(&request_arena);
  HttpMessageParser message_parser = {0};
  HttpRequest request = {0};
  if (read_http_request(socket_fd, &request_arena, &message_parser, &request))
  {
    HttpResponse response = handle_http_request(&message_parser, &request);
    send_http_response(socket_fd, &response);
  }
  close(socket_fd);
  temp_memory_end(temp_memory);
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

  u32 worker_count = os_get_processor_count();
  OsThread *workers = PushArray(&permanent_arena, OsThread, worker_count);
  if (!work_queue_init(&connection_queue, worker_count, workers))
  {
    printf("Failed to start the worker threads\n");
    close(server_fd);
    return -1;
  }

  // The accept loop is the queue's only producer
  while (1)
  {
    struct sockaddr_in connection_address = {0};
    socklen_t address_length = sizeof(connection_address);
    i32 new_socket_fd = accept(server_fd, (struct sockaddr *)&connection_address, &address_length);
    if (new_socket_fd < 0)
    {
      printf("Failed to establish connection. errno: %d\n", errno);
      continue;
    }
    printf("New connection\n");
    if (!work_queue_add_entry(&connection_queue, handle_connection, (void *)(uintptr_t)new_socket_fd))
    {
      // The request isn't read, so the client may see a reset instead of the response if it's still sending
      HttpResponse response = {
        .protocol_version = HttpProtocolVersion_1_1,
        .status_code = HttpStatus_Service_Unavailable,
      };
      send_http_response(new_socket_fd, &response);
      close(new_socket_fd);
    }
  }

  printf("Done\n");
  close(server_fd);
  return 0;
}
