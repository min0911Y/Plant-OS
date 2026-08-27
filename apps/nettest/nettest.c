#include <net.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

int main(int argc, char **argv) {
  unsigned int address = 0;
  for (int attempt = 0; attempt < 100; attempt++) {
    address = GetIP();
    if (address != 0) {
      break;
    }
    sleep(100);
  }
  if (address == 0) {
    printf("DHCP timeout.\n");
    return 1;
  }

  unsigned int gateway = (address & 0xffffff00u) | 2u;
  if (ping(gateway) != 0) {
    printf("ICMP gateway test failed.\n");
    return 2;
  }

  if (argc == 2) {
    char *end;
    long port = strtol(argv[1], &end, 10);
    if (*end != '\0' || port < 1 || port > 65535) {
      printf("Invalid TCP echo port.\n");
      return 3;
    }

    socket_t socket = Socket_Alloc(TCP_PROTOCOL);
    unsigned char reply[4];
    if (socket < 0 ||
        Socket_Init(socket, gateway, (unsigned short)port, address, 0) != 0 ||
        connect(socket) != 0) {
      printf("TCP connect failed.\n");
      return 4;
    }
    if (Socket_Send(socket, (const unsigned char *)"ping", 4) != 4 ||
        Socket_Recv(socket, reply, sizeof(reply)) != sizeof(reply) ||
        memcmp(reply, "pong", sizeof(reply)) != 0) {
      Socket_Free(socket);
      printf("TCP echo failed.\n");
      return 5;
    }
    Socket_Free(socket);
  } else if (argc != 1) {
    printf("Usage: nettest.bin [tcp-echo-port]\n");
    return 6;
  }
  printf("Network OK: %d.%d.%d.%d\n", address >> 24,
         (address >> 16) & 0xff, (address >> 8) & 0xff, address & 0xff);
  return 0;
}
