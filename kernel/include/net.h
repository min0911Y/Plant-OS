#ifndef KERNEL_NET_H
#define KERNEL_NET_H

#include <ctypes.h>

#define NET_SOCKET_LOCAL_PATH_MAX 108u

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
  NET_SOCKET_SYSCALL_COUNT,
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
};

typedef struct net_socket_syscall_request {
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

bool net_stack_start(void);
void net_stack_tick(void);
bool net_stack_ready(void);
uint32_t net_stack_ipv4(void);

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
int net_socket_resolve(const char *name, uint32_t length,
                       uint32_t *address);
void net_socket_tick(void);
void net_socket_cancel_waits(uint32_t tid, uint32_t generation);
void net_socket_task_cleanup(uint32_t owner_group);

#endif
