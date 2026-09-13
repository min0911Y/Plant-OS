#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

typedef struct plant_ifaddrs_node {
  struct ifaddrs value;
  struct sockaddr_in address;
  struct sockaddr_in netmask;
  struct sockaddr_in broadcast;
  char name[IFNAMSIZ];
} plant_ifaddrs_node_t;

static void plant_sockaddr_in(struct sockaddr_in *address, uint32_t value,
                              uint16_t port) {
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_port = port;
  address->sin_addr.s_addr = value;
  address->sin_len = sizeof(*address);
}

static plant_ifaddrs_node_t *plant_ifaddrs_add(
    struct ifaddrs **head, const char *name, unsigned flags, uint32_t address,
    uint32_t netmask, uint32_t broadcast) {
  plant_ifaddrs_node_t *node = malloc(sizeof(*node));
  if (node == NULL) {
    return NULL;
  }
  memset(node, 0, sizeof(*node));
  strncpy(node->name, name, sizeof(node->name) - 1);
  plant_sockaddr_in(&node->address, address, 0);
  plant_sockaddr_in(&node->netmask, netmask, 0);
  plant_sockaddr_in(&node->broadcast, broadcast, 0);
  node->value.ifa_name = node->name;
  node->value.ifa_flags = flags;
  node->value.ifa_addr = (struct sockaddr *)&node->address;
  node->value.ifa_netmask = (struct sockaddr *)&node->netmask;
  node->value.ifa_broadaddr = (struct sockaddr *)&node->broadcast;
  node->value.ifa_data = NULL;
  node->value.ifa_next = *head;
  *head = &node->value;
  return node;
}

int getifaddrs(struct ifaddrs **result) {
  if (result == NULL) {
    errno = EINVAL;
    return -1;
  }
  *result = NULL;

  if (plant_ifaddrs_add(result, "lo", IFF_UP | IFF_RUNNING | IFF_LOOPBACK,
                        htonl(INADDR_LOOPBACK), htonl(0xff000000u),
                        htonl(0x7fffffffu)) == NULL) {
    errno = ENOMEM;
    return -1;
  }

  struct in_addr address;
  if (socket_interface_address(&address) == 0 && address.s_addr != 0) {
    uint32_t host = ntohl(address.s_addr);
    uint32_t mask = 0xffffff00u;
    if (plant_ifaddrs_add(result, "en",
                          IFF_UP | IFF_RUNNING | IFF_BROADCAST | IFF_MULTICAST,
                          address.s_addr, htonl(mask), htonl(host | ~mask)) ==
        NULL) {
      freeifaddrs(*result);
      *result = NULL;
      errno = ENOMEM;
      return -1;
    }
  }
  return 0;
}

void freeifaddrs(struct ifaddrs *list) {
  while (list != NULL) {
    struct ifaddrs *next = list->ifa_next;
    free((plant_ifaddrs_node_t *)list);
    list = next;
  }
}

unsigned int if_nametoindex(const char *name) {
  if (name == NULL) {
    return 0;
  }
  if (!strcmp(name, "lo")) {
    return 1;
  }
  if (!strcmp(name, "en")) {
    struct in_addr address;
    return socket_interface_address(&address) == 0 && address.s_addr != 0 ? 2
                                                                            : 0;
  }
  return 0;
}

char *if_indextoname(unsigned int index, char *name) {
  if (name == NULL || index == 0 || index > 2 ||
      (index == 2 && if_nametoindex("en") == 0)) {
    errno = ENXIO;
    return NULL;
  }
  const char *source = index == 1 ? "lo" : "en";
  memcpy(name, source, strlen(source) + 1);
  return name;
}

static int plant_copy_text(char *destination, socklen_t capacity,
                           const char *source) {
  size_t length = strlen(source) + 1;
  if (destination == NULL) {
    return 0;
  }
  if (capacity < length) {
    return EAI_OVERFLOW;
  }
  memcpy(destination, source, length);
  return 0;
}

int getnameinfo(const struct sockaddr *address, socklen_t address_length,
                char *host, socklen_t host_length, char *service,
                socklen_t service_length, int flags) {
  const int valid_flags = NI_NUMERICHOST | NI_NUMERICSERV | NI_NOFQDN |
                          NI_NAMEREQD | NI_DGRAM | NI_NUMERICSCOPE;
  if ((flags & ~valid_flags) != 0 || address == NULL ||
      address_length < sizeof(struct sockaddr_in) ||
      address->sa_family != AF_INET) {
    return EAI_FAMILY;
  }
  const struct sockaddr_in *inet = (const struct sockaddr_in *)address;
  if (host != NULL) {
    if ((flags & NI_NAMEREQD) != 0) {
      return EAI_NONAME;
    }
    char numeric[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &inet->sin_addr, numeric, sizeof(numeric)) == NULL) {
      return EAI_FAIL;
    }
    int result = plant_copy_text(host, host_length, numeric);
    if (result != 0) {
      return result;
    }
  }
  if (service != NULL) {
    char numeric[NI_MAXSERV];
    uint16_t port = ntohs(inet->sin_port);
    char *out = numeric + sizeof(numeric) - 1;
    *out = '\0';
    do {
      *--out = (char)('0' + port % 10);
      port /= 10;
    } while (port != 0);
    int result = plant_copy_text(service, service_length, out);
    if (result != 0) {
      return result;
    }
  }
  return 0;
}

const char *gai_strerror(int error) {
  switch (error) {
  case 0:
    return "Success";
  case EAI_BADFLAGS:
    return "Invalid value for ai_flags";
  case EAI_NONAME:
    return "Name or service not known";
  case EAI_AGAIN:
    return "Temporary failure in name resolution";
  case EAI_FAIL:
    return "Non-recoverable failure in name resolution";
  case EAI_FAMILY:
    return "ai_family not supported";
  case EAI_SERVICE:
    return "Servname not supported for ai_socktype";
  case EAI_MEMORY:
    return "Memory allocation failure";
  case EAI_OVERFLOW:
    return "Argument buffer overflow";
  default:
    return "Unknown error";
  }
}

struct protoent *getprotobyname(const char *name) {
  static char protocol_name[] = "TCP";
  static char *aliases[] = {NULL};
  static struct protoent protocol = {protocol_name, aliases, IPPROTO_TCP};
  return name != NULL && (!strcmp(name, "TCP") || !strcmp(name, "tcp"))
             ? &protocol
             : NULL;
}
