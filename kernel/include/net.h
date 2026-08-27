#ifndef KERNEL_NET_H
#define KERNEL_NET_H

#include <ctypes.h>

enum net_syscall_operation {
  NET_SYSCALL_OPEN = 1,
  NET_SYSCALL_CLOSE,
  NET_SYSCALL_CONFIGURE,
  NET_SYSCALL_SEND,
  NET_SYSCALL_RECV,
  NET_SYSCALL_CONNECT,
  NET_SYSCALL_LISTEN,
  NET_SYSCALL_GET_IP,
  NET_SYSCALL_PING,
};

bool net_stack_start(void);
void net_stack_tick(void);
uint32_t net_stack_ip(void);

int net_socket_open(uint32_t owner_group, uint8_t protocol);
int net_socket_close(uint32_t owner_group, int handle);
int net_socket_configure(uint32_t owner_group, int handle,
                         uint32_t remote_ip, uint16_t remote_port,
                         uint32_t local_ip, uint16_t local_port);
int net_socket_send(uint32_t owner_group, int handle, const void *data,
                    uint32_t length);
int net_socket_recv(uint32_t owner_group, int handle, void *data,
                    uint32_t capacity);
int net_socket_connect(uint32_t owner_group, int handle);
int net_socket_listen(uint32_t owner_group, int handle);
int net_stack_ping(uint32_t address);
void net_task_cleanup(uint32_t owner_group);

#endif
