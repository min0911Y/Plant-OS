#ifndef KERNEL_NET_H
#define KERNEL_NET_H

#include <ctypes.h>
#include <io_poll.h>

#define NET_SOCKET_LOCAL_PATH_MAX 108u
#define NET_SOCKET_HANDLE_TAG 0x40000000u
#define NET_SOCKET_HANDLE_TAG_MASK 0xc0000000u

enum net_socket_syscall_operation {
  NET_SOCKET_SYSCALL_CREATE = 1,
  NET_SOCKET_SYSCALL_CLOSE,
  NET_SOCKET_SYSCALL_BIND,
  NET_SOCKET_SYSCALL_CONNECT,
  NET_SOCKET_SYSCALL_LISTEN,
  NET_SOCKET_SYSCALL_ACCEPT,
  NET_SOCKET_SYSCALL_SENDTO,
  NET_SOCKET_SYSCALL_RECVFROM,
  NET_SOCKET_SYSCALL_GETSOCKNAME,
  NET_SOCKET_SYSCALL_GETPEERNAME,
  NET_SOCKET_SYSCALL_RESOLVE,
  NET_SOCKET_SYSCALL_INTERFACE_ADDRESS,
  NET_SOCKET_SYSCALL_SET_OPTION,
  NET_SOCKET_SYSCALL_AVAILABLE,
  NET_SOCKET_SYSCALL_GET_OPTION,
  NET_SOCKET_SYSCALL_SHUTDOWN,
  NET_SOCKET_SYSCALL_PAIR,
  NET_SOCKET_SYSCALL_GET_FLAGS,
  NET_SOCKET_SYSCALL_SET_FLAGS,
  NET_SOCKET_SYSCALL_COUNT,
};

enum net_socket_option_level {
  NET_SOCKET_SOL_SOCKET = 1,
};

enum net_socket_option {
  NET_SOCKET_SO_RCVTIMEO = 1,
  NET_SOCKET_SO_DEBUG = 2,
  NET_SOCKET_SO_ACCEPTCONN = 4,
  NET_SOCKET_SO_REUSEADDR = 8,
  NET_SOCKET_SO_KEEPALIVE = 16,
  NET_SOCKET_SO_DONTROUTE = 32,
  NET_SOCKET_SO_BROADCAST = 64,
  NET_SOCKET_SO_LINGER = 256,
  NET_SOCKET_SO_OOBINLINE = 512,
  NET_SOCKET_SO_REUSEPORT = 1024,
  NET_SOCKET_SO_SNDBUF = 0x1001,
  NET_SOCKET_SO_RCVBUF = 0x1002,
  NET_SOCKET_SO_SNDLOWAT = 0x1003,
  NET_SOCKET_SO_RCVLOWAT = 0x1004,
  NET_SOCKET_SO_SNDTIMEO = 0x1005,
  NET_SOCKET_SO_ERROR = 0x1007,
  NET_SOCKET_SO_TYPE = 0x1008,
};

enum net_socket_protocol_option {
  NET_SOCKET_IPPROTO_IP = 0,
  NET_SOCKET_IPPROTO_TCP = 6,
  NET_SOCKET_IPPROTO_IPV6 = 41,
};

#define NET_SOCKET_TCP_NODELAY 1

enum net_socket_ip_option {
  NET_SOCKET_IP_TOS = 1,
  NET_SOCKET_IP_MULTICAST_TTL = 33,
  NET_SOCKET_IP_MULTICAST_LOOP = 34,
  NET_SOCKET_IP_MULTICAST_IF = 32,
};

enum net_socket_family {
  NET_SOCKET_AF_LOCAL = 1,
  NET_SOCKET_AF_INET = 2,
};

enum net_socket_type {
  NET_SOCKET_STREAM = 1,
  NET_SOCKET_DGRAM = 2,
  NET_SOCKET_RAW = 3,
};

enum net_socket_protocol {
  NET_SOCKET_PROTOCOL_ICMP = 1,
  NET_SOCKET_PROTOCOL_TCP = 6,
  NET_SOCKET_PROTOCOL_UDP = 17,
};

enum net_socket_message_flag {
  NET_SOCKET_MSG_DONTWAIT = 1u,
};

enum net_socket_result {
  NET_SOCKET_ERR_INVAL = -1,
  NET_SOCKET_ERR_NOMEM = -2,
  NET_SOCKET_ERR_NOENT = -3,
  NET_SOCKET_ERR_AGAIN = -4,
  NET_SOCKET_ERR_NOTCONN = -5,
  NET_SOCKET_ERR_TIMEDOUT = -6,
  NET_SOCKET_ERR_NETDOWN = -7,
  NET_SOCKET_ERR_ADDRINUSE = -8,
  NET_SOCKET_ERR_BUSY = -9,
  NET_SOCKET_ERR_PROTOCOL = -10,
  NET_SOCKET_ERR_INPROGRESS = -11,
  NET_SOCKET_ERR_PIPE = -12,
};

typedef struct {
  int64_t seconds;
  int64_t microseconds;
} net_socket_timeval_t;

typedef struct {
  int32_t on;
  int32_t seconds;
} net_socket_linger_t;

#define NET_SOCKET_OPTION_VALUE_MAX \
  ((sizeof(net_socket_timeval_t) > sizeof(net_socket_linger_t)) \
       ? sizeof(net_socket_timeval_t) \
       : sizeof(net_socket_linger_t))

typedef struct net_socket_syscall_request {
  int32_t socket;
  int32_t domain;
  int32_t type;
  int32_t protocol;
  int32_t backlog;
  uintptr_t address;
  uint32_t address_length;
  uintptr_t buffer;
  uint32_t length;
  uint32_t flags;
} net_socket_syscall_request_t;

typedef struct {
  uint16_t family;
  union {
    struct {
      uint32_t address;
      uint16_t port;
    } inet;
    struct {
      char path[NET_SOCKET_LOCAL_PATH_MAX];
      uint16_t length;
    } local;
  } value;
} net_socket_address_t;

/* Initializes lwIP and its always-available loopback interface. */
void net_stack_initialize(void);
/* Starts the optional Ethernet interface and asynchronous DHCP client. */
bool net_stack_start(void);
void net_stack_tick(void);
/* Drains queued traffic when address is loopback or the local Ethernet IP. */
void net_stack_poll_local(uint32_t address);
/* True after net_stack_initialize(), including when Ethernet is disabled. */
bool net_stack_ready(void);
uint32_t net_stack_ipv4(void);

short net_socket_poll(uint32_t owner_group, int handle, short events,
                      io_poll_watch_t *watch);
int net_socket_create(uint32_t owner_group, int domain, int type,
                      int protocol);
int net_socket_close(uint32_t owner_group, int handle);
int net_socket_bind(uint32_t owner_group, int handle,
                    const net_socket_address_t *address);
int net_socket_connect(uint32_t owner_group, int handle,
                       const net_socket_address_t *address);
int net_socket_listen(uint32_t owner_group, int handle, int backlog);
int net_socket_accept(uint32_t owner_group, int handle,
                      net_socket_address_t *peer_address);
int net_socket_sendto(uint32_t owner_group, int handle, const void *data,
                      uint32_t length, uint32_t flags,
                      const net_socket_address_t *address);
int net_socket_recvfrom(uint32_t owner_group, int handle, void *data,
                        uint32_t length, uint32_t flags,
                        net_socket_address_t *peer_address);
int net_socket_getname(uint32_t owner_group, int handle, bool peer,
                       net_socket_address_t *address);
int net_socket_set_option(uint32_t owner_group, int handle, int level,
                          int option, const void *value, uint32_t length);
int net_socket_get_option(uint32_t owner_group, int handle, int level,
                          int option, void *value, uint32_t *length);
int net_socket_shutdown(uint32_t owner_group, int handle, int how);
int net_socket_pair(uint32_t owner_group, int domain, int type, int protocol,
                    int handles[2]);
int net_socket_get_flags(uint32_t owner_group, int handle);
int net_socket_set_flags(uint32_t owner_group, int handle, int flags);
int net_socket_bytes_available(uint32_t owner_group, int handle,
                               uint32_t *bytes);
int net_socket_resolve(const char *name, uint32_t length,
                       uint32_t *address);
void net_socket_input_begin(void);
bool net_socket_input_end(void);
void net_socket_tick(void);
void net_socket_cancel_waits(uint32_t tid, uint32_t generation);
void net_socket_task_cleanup(uint32_t owner_group);

#endif
