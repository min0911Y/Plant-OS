#include <socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>

#define PING_PAYLOAD_BYTES 32u
#define PING_DHCP_WAIT_MS 10000u

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint16_t identifier;
  uint16_t sequence;
} icmp_echo_header_t;

static uint16_t ping_checksum(const uint8_t *data, uint32_t length) {
  uint32_t sum = 0;
  while (length > 1) {
    sum += ((uint16_t)data[0] << 8) | data[1];
    data += 2;
    length -= 2;
  }
  if (length != 0) {
    sum += (uint16_t)data[0] << 8;
  }
  while ((sum >> 16) != 0) {
    sum = (sum & 0xffffu) + (sum >> 16);
  }
  return (uint16_t)~sum;
}

static int ping_wait_for_network(void) {
  struct in_addr address;
  unsigned deadline = (unsigned)clock() + PING_DHCP_WAIT_MS;
  while ((int)((unsigned)clock() - deadline) < 0) {
    if (socket_interface_address(&address) == 0) {
      return 0;
    }
    sleep(100);
  }
  return SOCKET_ERR_AGAIN;
}

static int ping_parse_count(const char *text, unsigned *count) {
  if (text == NULL || *text == '\0') {
    return -1;
  }
  unsigned value = 0;
  while (*text != '\0') {
    if (*text < '0' || *text > '9' || value > 10 ||
        (value == 10 && *text > '0')) {
      return -1;
    }
    value = value * 10 + (unsigned)(*text - '0');
    text++;
  }
  if (value == 0 || value > 100) {
    return -1;
  }
  *count = value;
  return 0;
}

static int ping_reply_matches(const uint8_t *packet, uint32_t length,
                              uint16_t identifier, uint16_t sequence) {
  if (length < 20 || (packet[0] >> 4) != 4) {
    return 0;
  }
  uint32_t header_length = (packet[0] & 0x0fu) * 4u;
  if (header_length < 20 || length < header_length + sizeof(icmp_echo_header_t)) {
    return 0;
  }
  const icmp_echo_header_t *reply =
      (const icmp_echo_header_t *)(packet + header_length);
  return reply->type == 0 && reply->code == 0 &&
         ntohs(reply->identifier) == identifier &&
         ntohs(reply->sequence) == sequence;
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3) {
    printf("Usage: ping.bin <host-or-ipv4> [count]\n");
    return 1;
  }
  unsigned count = 4;
  if (argc == 3 && ping_parse_count(argv[2], &count) != 0) {
    printf("count must be between 1 and 100.\n");
    return 1;
  }
  struct in_addr literal;
  bool loopback_target =
      inet_pton(AF_INET, argv[1], &literal) != 0 &&
      literal.s_addr == htonl(INADDR_LOOPBACK);
  if (!loopback_target && strcmp(argv[1], "localhost") != 0 &&
      ping_wait_for_network() != 0) {
    printf("network address was not assigned.\n");
    return 2;
  }

  struct addrinfo *resolved = NULL;
  int gai = getaddrinfo(argv[1], NULL, NULL, &resolved);
  if (gai != 0 || resolved == NULL || resolved->ai_family != AF_INET) {
    printf("cannot resolve %s (DNS error %d).\n", argv[1], gai);
    freeaddrinfo(resolved);
    return 3;
  }
  struct sockaddr_in target = *(const struct sockaddr_in *)resolved->ai_addr;
  target.sin_port = 0;
  char target_text[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &target.sin_addr, target_text, sizeof(target_text)) == NULL) {
    strcpy(target_text, "?");
  }
  freeaddrinfo(resolved);

  socket_t socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (socket_fd < 0) {
    printf("cannot create ICMP socket: %d\n", socket_fd);
    return 4;
  }
  struct timeval receive_timeout = {.tv_sec = 1, .tv_usec = 0};
  int option_result =
      setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
                 sizeof(receive_timeout));
  if (option_result != 0) {
    printf("cannot set ICMP receive timeout: %d\n", option_result);
    socket_close(socket_fd);
    return 4;
  }

  printf("PING %s (%s): %d data bytes\n", argv[1], target_text,
         PING_PAYLOAD_BYTES);
  uint16_t identifier = (uint16_t)((unsigned)clock() ^ (unsigned)NowTaskID());
  unsigned received = 0;
  for (unsigned sequence = 1; sequence <= count; sequence++) {
    uint8_t request[sizeof(icmp_echo_header_t) + PING_PAYLOAD_BYTES];
    memset(request, 0xa5, sizeof(request));
    icmp_echo_header_t *header = (icmp_echo_header_t *)request;
    header->type = 8;
    header->code = 0;
    header->checksum = 0;
    header->identifier = htons(identifier);
    header->sequence = htons((uint16_t)sequence);
    header->checksum = htons(ping_checksum(request, sizeof(request)));

    uint64_t started = monotonic_ns();
    int sent = sendto(socket_fd, request, sizeof(request), 0,
                      (const struct sockaddr *)&target, sizeof(target));
    if (sent != sizeof(request)) {
      printf("send failed for seq=%d: %d\n", sequence, sent);
      sleep(1000);
      continue;
    }

    int matched = 0;
    for (;;) {
      uint8_t reply[1600];
      struct sockaddr_storage source;
      socklen_t source_length = sizeof(source);
      int size = recvfrom(socket_fd, reply, sizeof(reply), 0,
                          (struct sockaddr *)&source, &source_length);
      if (size < 0) {
        break;
      }
      if (!ping_reply_matches(reply, (uint32_t)size, identifier,
                              (uint16_t)sequence)) {
        continue;
      }
      const struct sockaddr_in *from = (const struct sockaddr_in *)&source;
      char source_text[INET_ADDRSTRLEN];
      if (source.ss_family != AF_INET ||
          inet_ntop(AF_INET, &from->sin_addr, source_text,
                    sizeof(source_text)) == NULL) {
        strcpy(source_text, target_text);
      }
      uint64_t elapsed_ns = monotonic_ns() - started;
      if (elapsed_ns < 1000ull) {
        printf("%d bytes from %s: icmp_seq=%d time<0.001 ms\n",
               size - (reply[0] & 0x0fu) * 4, source_text, sequence);
      } else {
        uint32_t elapsed_us = (uint32_t)((elapsed_ns + 500ull) / 1000ull);
        printf("%d bytes from %s: icmp_seq=%d time=%d.%03d ms\n",
               size - (reply[0] & 0x0fu) * 4, source_text, sequence,
               elapsed_us / 1000, elapsed_us % 1000);
      }
      received++;
      matched = 1;
      break;
    }
    if (!matched) {
      printf("Request timeout for icmp_seq %d\n", sequence);
    }
    if (sequence != count) {
      sleep(1000);
    }
  }
  socket_close(socket_fd);
  printf("--- %s ping statistics: %d transmitted, %d received ---\n", argv[1],
         count, received);
  return received == 0 ? 5 : 0;
}
