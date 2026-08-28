#ifndef PLOS_SOCKET_H
#define PLOS_SOCKET_H

#include <ctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int socket_t;
typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

struct timeval {
  int32_t tv_sec;
  int32_t tv_usec;
};

enum socket_family {
  AF_UNSPEC = 0,
  AF_LOCAL = 1,
  AF_INET = 2,
};

#define AF_UNIX AF_LOCAL

enum socket_type {
  SOCK_STREAM = 1,
  SOCK_DGRAM = 2,
  SOCK_RAW = 3,
};

enum socket_protocol {
  IPPROTO_ICMP = 1,
  IPPROTO_TCP = 6,
  IPPROTO_UDP = 17,
};

enum socket_message_flag {
  MSG_DONTWAIT = 1u,
};

enum socket_option_level {
  SOL_SOCKET = 1,
};

enum socket_option {
  SO_RCVTIMEO = 1,
};

enum socket_result {
  SOCKET_ERR_INVAL = -1,
  SOCKET_ERR_NOMEM = -2,
  SOCKET_ERR_NOENT = -3,
  SOCKET_ERR_AGAIN = -4,
  SOCKET_ERR_NOTCONN = -5,
  SOCKET_ERR_TIMEDOUT = -6,
  SOCKET_ERR_NETDOWN = -7,
  SOCKET_ERR_ADDRINUSE = -8,
  SOCKET_ERR_BUSY = -9,
  SOCKET_ERR_PROTOCOL = -10,
};

/* INADDR_* constants are host byte order; store them in s_addr with htonl(). */
#define INADDR_ANY ((uint32_t)0)
#define INADDR_LOOPBACK ((uint32_t)0x7f000001u)
#define INET_ADDRSTRLEN 16
#define UNIX_PATH_MAX 108

struct in_addr {
  uint32_t s_addr;
};

struct sockaddr {
  sa_family_t sa_family;
  uint8_t sa_data[14];
};

struct sockaddr_in {
  sa_family_t sin_family;
  uint16_t sin_port;
  struct in_addr sin_addr;
  uint8_t sin_zero[8];
};

struct sockaddr_un {
  sa_family_t sun_family;
  char sun_path[UNIX_PATH_MAX];
};

struct sockaddr_storage {
  sa_family_t ss_family;
  uint8_t __ss_padding[126];
};

struct addrinfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
  socklen_t ai_addrlen;
  struct sockaddr *ai_addr;
  char *ai_canonname;
  struct addrinfo *ai_next;
};

#define AI_PASSIVE 0x01
#define AI_CANONNAME 0x02
#define AI_NUMERICHOST 0x04

#define EAI_BADFLAGS -1
#define EAI_NONAME -2
#define EAI_AGAIN -3
#define EAI_FAIL -4
#define EAI_FAMILY -6
#define EAI_SERVICE -8
#define EAI_MEMORY -10

static inline uint16_t htons(uint16_t value) {
  return (uint16_t)((value << 8) | (value >> 8));
}

static inline uint16_t ntohs(uint16_t value) { return htons(value); }

static inline uint32_t htonl(uint32_t value) {
  return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
         ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
}

static inline uint32_t ntohl(uint32_t value) { return htonl(value); }

/* The following request ABI is private to libp and the kernel.  It remains in
 * this header so all freestanding clients use the exact fixed-width layout. */
enum socket_syscall_operation {
  SOCKET_SYSCALL_CREATE = 1,
  SOCKET_SYSCALL_CLOSE,
  SOCKET_SYSCALL_BIND,
  SOCKET_SYSCALL_CONNECT,
  SOCKET_SYSCALL_LISTEN,
  SOCKET_SYSCALL_ACCEPT,
  SOCKET_SYSCALL_SENDTO,
  SOCKET_SYSCALL_RECVFROM,
  SOCKET_SYSCALL_GETSOCKNAME,
  SOCKET_SYSCALL_GETPEERNAME,
  SOCKET_SYSCALL_RESOLVE,
  SOCKET_SYSCALL_INTERFACE_ADDRESS,
  SOCKET_SYSCALL_SET_OPTION,
  SOCKET_SYSCALL_COUNT,
};

typedef struct socket_syscall_request {
  int32_t socket;
  int32_t domain;
  int32_t type;
  int32_t protocol;
  int32_t backlog;
  uint32_t address;
  uint32_t address_length;
  uint32_t buffer;
  uint32_t length;
  uint32_t flags;
} socket_syscall_request_t;

socket_t socket(int domain, int type, int protocol);
int socket_close(socket_t socket);
int bind(socket_t socket, const struct sockaddr *address, socklen_t length);
int connect(socket_t socket, const struct sockaddr *address, socklen_t length);
int listen(socket_t socket, int backlog);
socket_t accept(socket_t socket, struct sockaddr *address, socklen_t *length);
int send(socket_t socket, const void *data, uint32_t length, uint32_t flags);
int recv(socket_t socket, void *data, uint32_t length, uint32_t flags);
int sendto(socket_t socket, const void *data, uint32_t length, uint32_t flags,
           const struct sockaddr *address, socklen_t address_length);
int recvfrom(socket_t socket, void *data, uint32_t length, uint32_t flags,
             struct sockaddr *address, socklen_t *address_length);
int getsockname(socket_t socket, struct sockaddr *address, socklen_t *length);
int getpeername(socket_t socket, struct sockaddr *address, socklen_t *length);
int setsockopt(socket_t socket, int level, int option, const void *value,
               socklen_t length);

int inet_pton(int family, const char *text, void *address);
const char *inet_ntop(int family, const void *address, char *text,
                      socklen_t text_length);
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result);
void freeaddrinfo(struct addrinfo *result);

/* Reports the DHCP-assigned IPv4 address in network byte order. */
int socket_interface_address(struct in_addr *address);

#ifdef __cplusplus
}
#endif

#endif
