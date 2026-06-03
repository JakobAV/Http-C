#include "base_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

#define DEFAULT_PORT 8888

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

char buffer[KB(1)];

int main()
{

  i32 server_fd = create_server(DEFAULT_PORT);
  if (server_fd == -1)
  {
    printf("Failed to create server\n");
    return -1;
  }

  u32 address_length = sizeof(struct sockaddr_in);
  while (1)
  {
    struct sockaddr_in connection_address = {0};
    i32 new_socket_fd = accept(server_fd, (struct sockaddr *)&connection_address, (socklen_t*)&address_length);
    if(new_socket_fd < 0)
    {
      printf("Failed to establish connection. Error code: %d\n", new_socket_fd);
      continue;
    }

    printf("New connection\n");

    i64 bytes_read = read(new_socket_fd, buffer, sizeof(buffer));
    if (bytes_read > 0)
    {
      buffer[bytes_read] = '\0';
      printf("Client sent %s\n", buffer);
    }
    StringLit response = STR_LIT("HTTP/1.1 204 OK\n\n");
    send(new_socket_fd, response.text, response.length, 0);
  }

  printf("Done\n");
  close(server_fd);
  return 0;
}
