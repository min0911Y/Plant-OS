#include <errno.h>
#include <socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint16_t identifier;
  uint16_t sequence;
} nettest_icmp_header_t;

static uint16_t nettest_checksum(const uint8_t *data, uint32_t length) {
  uint32_t sum = 0;
  while (length > 1) {
    sum += ((uint16_t)data[0] << 8) | data[1];
    data += 2;
    length -= 2;
  }
  if (length != 0) {
    sum += (uint16_t)data[0] << 8;
  }
  while (sum >> 16) {
    sum = (sum & 0xffffu) + (sum >> 16);
  }
  return (uint16_t)~sum;
}

static void nettest_local_address(struct sockaddr_un *address,
                                  const char *path) {
  memset(address, 0, sizeof(*address));
  address->sun_family = AF_LOCAL;
  strcpy(address->sun_path, path);
}

static void nettest_loopback_address(struct sockaddr_in *address) {
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static int nettest_local_dgram(void) {
  struct sockaddr_un server_address;
  nettest_local_address(&server_address, "nettest.dgram");
  socket_t server = socket(AF_LOCAL, SOCK_DGRAM, 0);
  socket_t client = socket(AF_LOCAL, SOCK_DGRAM, 0);
  if (server < 0 || client < 0 ||
      bind(server, (const struct sockaddr *)&server_address,
           sizeof(server_address)) != 0 ||
      sendto(client, "dgram", 5, 0, (const struct sockaddr *)&server_address,
             sizeof(server_address)) != 5) {
    socket_close(server);
    socket_close(client);
    return -1;
  }
  char buffer[8];
  struct sockaddr_storage source;
  socklen_t source_length = sizeof(source);
  int received = recvfrom(server, buffer, sizeof(buffer), 0,
                          (struct sockaddr *)&source, &source_length);
  socket_close(server);
  socket_close(client);
  return received == 5 && memcmp(buffer, "dgram", 5) == 0 &&
                 source.ss_family == AF_LOCAL
             ? 0
             : -1;
}

static int nettest_local_stream(void) {
  struct sockaddr_un listener_address;
  nettest_local_address(&listener_address, "nettest.stream");
  int child = fork();
  if (child < 0) {
    return -1;
  }
  if (child == 0) {
    socket_t listener = socket(AF_LOCAL, SOCK_STREAM, 0);
    socket_t server = -1;
    char buffer[8];
    int result = listener >= 0 &&
                 bind(listener, (const struct sockaddr *)&listener_address,
                      sizeof(listener_address)) == 0 &&
                 listen(listener, 2) == 0
                     ? 0
                     : -1;
    if (result == 0) {
      server = accept(listener, NULL, NULL);
      result = server >= 0 && recv(server, buffer, sizeof(buffer), 0) == 4 &&
                       memcmp(buffer, "ping", 4) == 0 &&
                       send(server, "pong", 4, 0) == 4
                   ? 0
                   : -1;
    }
    if (server >= 0) {
      socket_close(server);
    }
    if (listener >= 0) {
      socket_close(listener);
    }
    exit(result == 0 ? 0 : 1);
  }

  socket_t client = socket(AF_LOCAL, SOCK_STREAM, 0);
  int connected = -1;
  for (unsigned attempt = 0; client >= 0 && attempt < 100; attempt++) {
    connected = connect(client, (const struct sockaddr *)&listener_address,
                        sizeof(listener_address));
    if (connected == 0 || (connected < 0 && errno != ENOENT)) {
      break;
    }
    sleep(10);
  }
  char buffer[8];
  int result = connected == 0 && send(client, "ping", 4, 0) == 4 &&
               recv(client, buffer, sizeof(buffer), 0) == 4 &&
               memcmp(buffer, "pong", 4) == 0
                   ? 0
                   : -1;
  if (client >= 0) {
    socket_close(client);
  }
  return result == 0 && waittid((unsigned)child) == 0 ? 0 : -1;
}

static int nettest_loopback_udp(void) {
  struct sockaddr_in endpoint;
  nettest_loopback_address(&endpoint);
  socket_t server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  socket_t client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int result = server >= 0 && client >= 0 &&
                       bind(server, (const struct sockaddr *)&endpoint,
                            sizeof(endpoint)) == 0
                   ? 0
                   : -1;
  socklen_t endpoint_length = sizeof(endpoint);
  if (result == 0 &&
      getsockname(server, (struct sockaddr *)&endpoint, &endpoint_length) != 0) {
    result = -1;
  }
  if (result == 0 &&
      sendto(client, "loop", 4, 0, (const struct sockaddr *)&endpoint,
             sizeof(endpoint)) != 4) {
    result = -1;
  }

  char buffer[8];
  struct sockaddr_storage source;
  socklen_t source_length = sizeof(source);
  int received = result == 0
                     ? recvfrom(server, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&source, &source_length)
                     : -1;
  const struct sockaddr_in *from = (const struct sockaddr_in *)&source;
  if (received != 4 || memcmp(buffer, "loop", 4) != 0 ||
      source.ss_family != AF_INET ||
      from->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
    result = -1;
  }
  if (server >= 0) {
    socket_close(server);
  }
  if (client >= 0) {
    socket_close(client);
  }
  return result;
}

static int nettest_loopback_stream(void) {
  struct sockaddr_in endpoint;
  nettest_loopback_address(&endpoint);
  socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  socket_t server = -1;
  socklen_t endpoint_length = sizeof(endpoint);
  if (listener < 0 ||
      bind(listener, (const struct sockaddr *)&endpoint, sizeof(endpoint)) != 0 ||
      getsockname(listener, (struct sockaddr *)&endpoint, &endpoint_length) != 0 ||
      listen(listener, 1) != 0) {
    if (listener >= 0) {
      socket_close(listener);
    }
    return -1;
  }

  int child = fork();
  if (child < 0) {
    socket_close(listener);
    return -1;
  }
  if (child == 0) {
    socket_t client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[8];
    int result = client >= 0 &&
                 connect(client, (const struct sockaddr *)&endpoint,
                         sizeof(endpoint)) == 0 &&
                 send(client, "ping", 4, 0) == 4 &&
                 recv(client, buffer, sizeof(buffer), 0) == 4 &&
                 memcmp(buffer, "pong", 4) == 0
                     ? 0
                     : -1;
    if (client >= 0) {
      socket_close(client);
    }
    exit(result == 0 ? 0 : 1);
  }

  char buffer[8];
  server = accept(listener, NULL, NULL);
  int result = server >= 0 && recv(server, buffer, sizeof(buffer), 0) == 4 &&
                       memcmp(buffer, "ping", 4) == 0 &&
                       send(server, "pong", 4, 0) == 4
                   ? 0
                   : -1;
  bool child_waited = false;
  if (result == 0) {
    result = waittid((unsigned)child) == 0 ? 0 : -1;
    child_waited = true;
  }
  if (server >= 0) {
    socket_close(server);
  }
  socket_close(listener);
  if (!child_waited) {
    (void)waittid((unsigned)child);
  }
  return result;
}

static int nettest_ping(const struct sockaddr_in *target,
                        uint64_t *elapsed_ns) {
  socket_t socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (socket_fd < 0) {
    return -1;
  }
  struct timeval receive_timeout = {.tv_sec = 2, .tv_usec = 0};
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
                 sizeof(receive_timeout)) != 0) {
    socket_close(socket_fd);
    return -1;
  }
  uint8_t request[sizeof(nettest_icmp_header_t) + 16];
  memset(request, 0x5a, sizeof(request));
  nettest_icmp_header_t *header = (nettest_icmp_header_t *)request;
  uint16_t identifier = (uint16_t)((unsigned)clock() ^ (unsigned)NowTaskID());
  header->type = 8;
  header->code = 0;
  header->checksum = 0;
  header->identifier = htons(identifier);
  header->sequence = htons(1);
  header->checksum = htons(nettest_checksum(request, sizeof(request)));
  uint64_t started = monotonic_ns();
  if (sendto(socket_fd, request, sizeof(request), 0,
             (const struct sockaddr *)target, sizeof(*target)) !=
      sizeof(request)) {
    socket_close(socket_fd);
    return -1;
  }

  int result = -1;
  for (;;) {
    uint8_t packet[1600];
    int length = recv(socket_fd, packet, sizeof(packet), 0);
    if (length < 0) {
      break;
    }
    if (length < 20 || (packet[0] >> 4) != 4) {
      continue;
    }
    uint32_t ip_length = (packet[0] & 0x0fu) * 4u;
    if (ip_length + sizeof(nettest_icmp_header_t) > (uint32_t)length) {
      continue;
    }
    const nettest_icmp_header_t *reply =
        (const nettest_icmp_header_t *)(packet + ip_length);
    if (reply->type == 0 && reply->code == 0 &&
        ntohs(reply->identifier) == identifier && ntohs(reply->sequence) == 1) {
      if (elapsed_ns != NULL) {
        *elapsed_ns = monotonic_ns() - started;
      }
      result = 0;
      break;
    }
  }
  socket_close(socket_fd);
  return result;
}

static int nettest_monotonic_clock(void) {
  uint64_t previous = monotonic_ns();
  for (unsigned attempt = 0; attempt < 16; attempt++) {
    uint64_t current = monotonic_ns();
    if (current > previous && current - previous < 10000000ull) {
      return 0;
    }
    previous = current;
  }
  return -1;
}

static int nettest_timeout_options(void) {
  socket_t socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_fd < 0)
    return -1;
  int result = -1;
  const int options[] = {SO_RCVTIMEO, SO_SNDTIMEO};
  for (unsigned i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
    struct timeval value = {.tv_sec = 1, .tv_usec = 230000};
    struct {
      struct timeval time;
      uint32_t guard;
    } output = {.guard = 0x12345678};
    socklen_t length = sizeof(output.time);
    if (setsockopt(socket_fd, SOL_SOCKET, options[i], &value, sizeof(value)) ||
        getsockopt(socket_fd, SOL_SOCKET, options[i], &output.time, &length) ||
        length != sizeof(output.time) || output.time.tv_sec != value.tv_sec ||
        output.time.tv_usec != value.tv_usec || output.guard != 0x12345678)
      goto done;
    if (setsockopt(socket_fd, SOL_SOCKET, options[i], &value,
                   sizeof(value) - 1) != -1 ||
        errno != EINVAL)
      goto done;
    length = sizeof(output.time) - 1;
    if (getsockopt(socket_fd, SOL_SOCKET, options[i], &output.time, &length) !=
            -1 ||
        errno != EINVAL)
      goto done;
    value.tv_usec = 1000000;
    if (setsockopt(socket_fd, SOL_SOCKET, options[i], &value, sizeof(value)) !=
            -1 ||
        errno != EINVAL)
      goto done;
  }
  struct sockaddr_in endpoint;
  nettest_loopback_address(&endpoint);
  struct timeval timeout = {.tv_sec = 0, .tv_usec = 20000};
  if (bind(socket_fd, (const struct sockaddr *)&endpoint, sizeof(endpoint)) ||
      setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
    goto done;
  char byte;
  result = recv(socket_fd, &byte, sizeof(byte), 0) == -1 && errno == ETIMEDOUT
               ? 0
               : -1;
done:
  socket_close(socket_fd);
  return result;
}

static int nettest_loopback(void) {
  const struct {
    const char *name;
    int (*run)(void);
  } tests[] = {{"clock", nettest_monotonic_clock},
               {"timeouts", nettest_timeout_options},
               {"UDP", nettest_loopback_udp},
               {"TCP", nettest_loopback_stream}};
  for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    if (tests[i].run() != 0) {
      logkf("NETTEST FAIL %s errno=%d\n", tests[i].name, errno);
      return -1;
    }
  }
  struct sockaddr_in target;
  nettest_loopback_address(&target);
  uint64_t elapsed_ns = 0;
  if (nettest_ping(&target, &elapsed_ns) != 0) {
    logkf("NETTEST FAIL ICMP errno=%d\n", errno);
    return -1;
  }
  if (elapsed_ns >= 10000000ull) {
    logkf("NETTEST FAIL ICMP latency=%llu ns\n",
          (unsigned long long)elapsed_ns);
    return -1;
  }
  logkf("NETTEST LOOPBACK PASS latency=%llu ns\n",
        (unsigned long long)elapsed_ns);
  return 0;
}

static int nettest_udp(const struct sockaddr_in *target) {
  struct sockaddr_in endpoint = *target;
  endpoint.sin_port = htons(9);
  socket_t socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int result = socket_fd >= 0 &&
               connect(socket_fd, (const struct sockaddr *)&endpoint,
                       sizeof(endpoint)) == 0 &&
               send(socket_fd, "u", 1, 0) == 1
                   ? 0
                   : -1;
  if (socket_fd >= 0) {
    socket_close(socket_fd);
  }
  return result;
}

static int nettest_tcp(const struct sockaddr_in *target, const char *text) {
  char *end;
  long port = strtol(text, &end, 10);
  if (*end != '\0' || port < 1 || port > 65535) {
    return -1;
  }
  struct sockaddr_in endpoint = *target;
  endpoint.sin_port = htons((uint16_t)port);
  socket_t socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  unsigned char reply[4];
  int result = socket_fd >= 0 &&
               connect(socket_fd, (const struct sockaddr *)&endpoint,
                       sizeof(endpoint)) == 0 &&
               send(socket_fd, "ping", 4, 0) == 4 &&
               recv(socket_fd, reply, sizeof(reply), 0) == sizeof(reply) &&
               memcmp(reply, "pong", sizeof(reply)) == 0
                   ? 0
                   : -1;
  if (socket_fd >= 0) {
    socket_close(socket_fd);
  }
  return result;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "loopback") == 0) {
    if (nettest_loopback() != 0) {
      printf("IPv4 loopback test failed.\n");
      return 2;
    }
    printf("IPv4 loopback OK.\n");
    return 0;
  }
  if (argc != 1 && argc != 2) {
    printf("Usage: nettest.bin [loopback|tcp-echo-port]\n");
    return 1;
  }
  if (nettest_loopback() != 0) {
    printf("IPv4 loopback test failed.\n");
    return 2;
  }
  if (nettest_local_dgram() != 0 || nettest_local_stream() != 0) {
    printf("AF_LOCAL socket test failed.\n");
    return 3;
  }

  struct in_addr local;
  memset(&local, 0, sizeof(local));
  for (unsigned attempt = 0; attempt < 100; attempt++) {
    if (socket_interface_address(&local) == 0) {
      break;
    }
    sleep(100);
  }
  if (local.s_addr == 0) {
    printf("DHCP timeout.\n");
    return 4;
  }

  struct sockaddr_in gateway;
  memset(&gateway, 0, sizeof(gateway));
  gateway.sin_family = AF_INET;
  gateway.sin_addr.s_addr = htonl((ntohl(local.s_addr) & 0xffffff00u) | 2u);
  if (nettest_udp(&gateway) != 0) {
    printf("UDP gateway send test failed.\n");
    return 5;
  }
  if (nettest_ping(&gateway, NULL) != 0) {
    printf("ICMP gateway test failed.\n");
    return 6;
  }
  if (argc == 2 && nettest_tcp(&gateway, argv[1]) != 0) {
    printf("TCP echo test failed.\n");
    return 7;
  }

  uint32_t host = ntohl(local.s_addr);
  printf("Network OK: %d.%d.%d.%d\n", host >> 24, (host >> 16) & 0xff,
         (host >> 8) & 0xff, host & 0xff);
  return 0;
}
