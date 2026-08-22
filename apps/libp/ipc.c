// Plant OS IPC 用户态封装
#include <ipc.h>

#define IPC_SYSCALL 0x5d
#define IPC_SYS_SEND 0x01
#define IPC_SYS_RECV 0x02
#define IPC_SYS_PEEK 0x03
#define IPC_SYS_PENDING 0x04
#define IPC_SYS_REGISTER 0x05
#define IPC_SYS_UNREGISTER 0x06
#define IPC_SYS_LOOKUP 0x07
#define IPC_SYS_GENERATION 0x08

static int ipc_do_syscall(unsigned sub, unsigned arg1, unsigned arg2) {
  int ret;
  unsigned clobber_b, clobber_c, clobber_d;
  asm volatile("int $0x36"
               : "=a"(ret), "=b"(clobber_b), "=c"(clobber_c), "=d"(clobber_d)
               : "a"(IPC_SYSCALL), "1"(sub), "2"(arg1), "3"(arg2)
               : "memory");
  return ret;
}

int ipc_send_msg(ipc_msg_t *msg) {
  if (!msg) {
    return IPC_ERR_INVAL;
  }
  return ipc_do_syscall(IPC_SYS_SEND, (unsigned)msg, 0);
}

int ipc_recv_msg(ipc_msg_t *msg) {
  if (!msg) {
    return IPC_ERR_INVAL;
  }
  return ipc_do_syscall(IPC_SYS_RECV, (unsigned)msg, 0);
}

int ipc_peek_msg(ipc_msg_t *msg) {
  if (!msg) {
    return IPC_ERR_INVAL;
  }
  return ipc_do_syscall(IPC_SYS_PEEK, (unsigned)msg, 0);
}

int ipc_pending(void) { return ipc_do_syscall(IPC_SYS_PENDING, 0, 0); }

int ipc_register(const char *name) {
  return ipc_do_syscall(IPC_SYS_REGISTER, (unsigned)name, 0);
}

int ipc_unregister(const char *name) {
  return ipc_do_syscall(IPC_SYS_UNREGISTER, (unsigned)name, 0);
}

int ipc_lookup(const char *name, unsigned *generation) {
  return ipc_do_syscall(IPC_SYS_LOOKUP, (unsigned)name, (unsigned)generation);
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
