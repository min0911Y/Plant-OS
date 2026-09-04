// Plant OS IPC 用户态封装
#include <ipc.h>
#include "syscall_internal.h"

#define IPC_SYSCALL 0x5d
#define IPC_SYS_SEND 0x01
#define IPC_SYS_RECV 0x02
#define IPC_SYS_PEEK 0x03
#define IPC_SYS_PENDING 0x04
#define IPC_SYS_REGISTER 0x05
#define IPC_SYS_UNREGISTER 0x06
#define IPC_SYS_LOOKUP 0x07
#define IPC_SYS_GENERATION 0x08

static int ipc_do_syscall(unsigned sub, uintptr_t arg1, uintptr_t arg2) {
  return (int)libp_syscall3(IPC_SYSCALL, sub, arg1, arg2);
}

int ipc_send_msg(ipc_msg_t *msg) {
  if (!msg) {
    return IPC_ERR_INVAL;
  }
  return ipc_do_syscall(IPC_SYS_SEND, (uintptr_t)msg, 0);
}

int ipc_recv_msg(ipc_msg_t *msg) {
  if (!msg) {
    return IPC_ERR_INVAL;
  }
  return ipc_do_syscall(IPC_SYS_RECV, (uintptr_t)msg, 0);
}

int ipc_peek_msg(ipc_msg_t *msg) {
  if (!msg) {
    return IPC_ERR_INVAL;
  }
  return ipc_do_syscall(IPC_SYS_PEEK, (uintptr_t)msg, 0);
}

int ipc_pending(void) { return ipc_do_syscall(IPC_SYS_PENDING, 0, 0); }

int ipc_register(const char *name) {
  return ipc_do_syscall(IPC_SYS_REGISTER, (uintptr_t)name, 0);
}

int ipc_unregister(const char *name) {
  return ipc_do_syscall(IPC_SYS_UNREGISTER, (uintptr_t)name, 0);
}

int ipc_lookup(const char *name, unsigned *generation) {
  return ipc_do_syscall(IPC_SYS_LOOKUP, (uintptr_t)name,
                        (uintptr_t)generation);
}

int ipc_generation(void) { return ipc_do_syscall(IPC_SYS_GENERATION, 0, 0); }

int ipc_send_to(unsigned tid, unsigned type, unsigned id, const void *data,
                unsigned size, unsigned timeout_ms) {
  ipc_msg_t msg;
  msg.peer_tid = tid;
  msg.peer_generation = 0;
  msg.type = type;
  msg.id = id;
  msg.size = size;
  msg.flags = 0;
  msg.timeout_ms = timeout_ms;
  msg.from_filter = IPC_ANY_TID;
  msg.data = (void *)data;
  return ipc_send_msg(&msg);
}

int ipc_recv_from(unsigned from_tid, void *buf, unsigned bufsize,
                  ipc_msg_t *out, unsigned timeout_ms) {
  ipc_msg_t msg;
  int result;
  msg.peer_tid = 0;
  msg.peer_generation = 0;
  msg.type = 0;
  msg.id = 0;
  msg.size = bufsize;
  msg.flags = 0;
  msg.timeout_ms = timeout_ms;
  msg.from_filter = from_tid;
  msg.data = buf;
  result = ipc_recv_msg(&msg);
  if (out) {
    *out = msg;
  }
  return result;
}

int ipc_recv_any(void *buf, unsigned bufsize, ipc_msg_t *out,
                 unsigned timeout_ms) {
  return ipc_recv_from(IPC_ANY_TID, buf, bufsize, out, timeout_ms);
}
