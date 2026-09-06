// Plant OS 用户态 RPC 库
// 详见 apps/include/rpc.h 的说明
#include <rpc.h>
#include <string.h>
#include <syscall.h>
#include <time.h>

#define RPC_REPLY_TIMEOUT_MS 2000 // 送应答时最多等对方队列腾出位置的时间

typedef struct {
  unsigned opcode;
  rpc_handler_t handler;
  int used;
} rpc_slot_t;

typedef struct {
  int used;
  unsigned id;
  int status;
  unsigned len;
  void *data;
} rpc_stash_t;

static rpc_slot_t rpc_handlers[RPC_MAX_HANDLER];
static unsigned rpc_outstanding[RPC_MAX_NESTING]; // 正在等应答的调用序号
static int rpc_depth;
static rpc_stash_t rpc_stash[RPC_MAX_NESTING]; // 替外层调用先收下来的应答
static unsigned rpc_next_id = 1;
static int rpc_stop_flag;

static int rpc_map_ipc_error(int rc) {
  switch (rc) {
  case IPC_ERR_NOTASK:
    return RPC_ERR_NO_SERVICE;
  case IPC_ERR_TIMEOUT:
    return RPC_ERR_TIMEOUT;
  case IPC_ERR_TOOBIG:
    return RPC_ERR_TOOBIG;
  case IPC_ERR_NOMEM:
    return RPC_ERR_NOMEM;
  case IPC_ERR_FULL:
    return RPC_ERR_BUSY;
  case IPC_ERR_INVAL:
    return RPC_ERR_INVAL;
  default:
    return RPC_ERR_TRANSPORT;
  }
}

static rpc_handler_t rpc_find_handler(unsigned opcode) {
  for (int i = 0; i < RPC_MAX_HANDLER; i++) {
    if (rpc_handlers[i].used && rpc_handlers[i].opcode == opcode) {
      return rpc_handlers[i].handler;
    }
  }
  return NULL;
}

static int rpc_is_outstanding(unsigned id) {
  for (int i = 0; i < rpc_depth; i++) {
    if (rpc_outstanding[i] == id) {
      return 1;
    }
  }
  return 0;
}

// 收到的应答不属于当前正在等的调用（属于外层调用）时先存起来
static void rpc_stash_put(unsigned id, int status, const void *data,
                          unsigned len) {
  for (int i = 0; i < RPC_MAX_NESTING; i++) {
    if (rpc_stash[i].used) {
      continue;
    }
    rpc_stash[i].data = NULL;
    if (len) {
      rpc_stash[i].data = malloc(len);
      if (!rpc_stash[i].data) {
        return;
      }
      memcpy(rpc_stash[i].data, data, len);
    }
    rpc_stash[i].used = 1;
    rpc_stash[i].id = id;
    rpc_stash[i].status = status;
    rpc_stash[i].len = len;
    return;
  }
}

// found 输出是否找到；返回值是该调用的状态码
static int rpc_stash_take(unsigned id, void *ret, unsigned ret_cap,
                          unsigned *ret_len, int *found) {
  *found = 0;
  for (int i = 0; i < RPC_MAX_NESTING; i++) {
    if (!rpc_stash[i].used || rpc_stash[i].id != id) {
      continue;
    }
    int status = rpc_stash[i].status;
    unsigned len = rpc_stash[i].len;
    *found = 1;
    if (len > ret_cap) {
      status = RPC_ERR_TOOBIG;
      len = 0;
    } else if (len && ret) {
      memcpy(ret, rpc_stash[i].data, len);
    }
    if (ret_len) {
      *ret_len = len;
    }
    if (rpc_stash[i].data) {
      free(rpc_stash[i].data);
    }
    rpc_stash[i].data = NULL;
    rpc_stash[i].used = 0;
    return status;
  }
  return RPC_OK;
}

// 处理一条请求；通知不生成应答，避免无消费者的 reply 占满队列。
static void rpc_dispatch(ipc_msg_t *msg, void *buffer) {
  rpc_wire_t *wire = (rpc_wire_t *)buffer;
  unsigned arg_len = msg->size - sizeof(rpc_wire_t);
  rpc_handler_t handler = rpc_find_handler(wire->opcode);
  unsigned ret_len = 0;
  int status = RPC_ERR_BAD_OPCODE;
  int reply_required = msg->type == RPC_TYPE_REQUEST;
  char reply[sizeof(rpc_wire_t) + RPC_MAX_PAYLOAD];

  if (wire->len < arg_len) {
    arg_len = wire->len;
  }
  if (handler) {
    rpc_call_t call;
    call.opcode = wire->opcode;
    call.caller_tid = msg->peer_tid;
    call.caller_generation = msg->peer_generation;
    call.call_id = msg->id;
    call.arg = (const char *)buffer + sizeof(rpc_wire_t);
    call.arg_len = arg_len;
    call.ret = reply_required ? reply + sizeof(rpc_wire_t) : NULL;
    call.ret_cap = reply_required ? RPC_MAX_PAYLOAD : 0;
    call.ret_len = 0;
    status = handler(&call);
    ret_len = call.ret_len > RPC_MAX_PAYLOAD ? RPC_MAX_PAYLOAD : call.ret_len;
  }
  if (!reply_required || status == RPC_NO_REPLY) {
    return;
  }

  rpc_wire_t *out_wire = (rpc_wire_t *)reply;
  out_wire->opcode = wire->opcode;
  out_wire->status = status;
  out_wire->len = ret_len;

  ipc_msg_t out;
  out.peer_tid = msg->peer_tid;
  out.peer_generation = msg->peer_generation;
  out.type = RPC_TYPE_REPLY;
  out.id = msg->id;
  out.size = sizeof(rpc_wire_t) + ret_len;
  out.flags = 0;
  out.timeout_ms = RPC_REPLY_TIMEOUT_MS;
  out.from_filter = IPC_ANY_TID;
  out.data = reply;
  ipc_send_msg(&out);
}

// 等 call_id 的应答；期间收到的请求会就地处理，因此支持互相调用
static int rpc_wait_reply(unsigned call_id, void *ret, unsigned ret_cap,
                          unsigned *ret_len, unsigned timeout_ms) {
  unsigned deadline = 0;
  int use_deadline = timeout_ms != 0;
  int found = 0;
  int status = rpc_stash_take(call_id, ret, ret_cap, ret_len, &found);
  char rx[IPC_MAX_MSG_SIZE];
  int result;

  if (found) {
    return status;
  }
  if (use_deadline) {
    deadline = (unsigned)clock() + timeout_ms;
  }
  for (;;) {
    unsigned wait_ms = 0;
    if (use_deadline) {
      unsigned now = (unsigned)clock();
      if ((int)(now - deadline) >= 0) {
        result = RPC_ERR_TIMEOUT;
        break;
      }
      wait_ms = deadline - now;
    }
    ipc_msg_t got;
    int rc = ipc_recv_any(rx, sizeof(rx), &got, wait_ms);
    if (rc < 0) {
      result = rpc_map_ipc_error(rc);
      break;
    }
    if (got.size < sizeof(rpc_wire_t)) {
      continue; // 不是 RPC 消息，丢掉
    }
    rpc_wire_t *wire = (rpc_wire_t *)rx;
    if (got.type == RPC_TYPE_REPLY) {
      if (got.id == call_id) {
        unsigned len = wire->len;
        if (len > got.size - sizeof(rpc_wire_t)) {
          len = got.size - sizeof(rpc_wire_t);
        }
        if (len > ret_cap) {
          result = RPC_ERR_TOOBIG;
          break;
        }
        if (len && ret) {
          memcpy(ret, rx + sizeof(rpc_wire_t), len);
        }
        if (ret_len) {
          *ret_len = len;
        }
        result = wire->status;
        break;
      }
      if (rpc_is_outstanding(got.id)) {
        // 是外层调用的应答，先收着，等外层自己来取
        unsigned len = wire->len;
        if (len > got.size - sizeof(rpc_wire_t)) {
          len = got.size - sizeof(rpc_wire_t);
        }
        rpc_stash_put(got.id, wire->status, rx + sizeof(rpc_wire_t), len);
      }
      continue; // 其它情况是迟到的应答，丢掉
    }
    if (got.type == RPC_TYPE_REQUEST || got.type == RPC_TYPE_NOTIFY) {
      rpc_dispatch(&got, rx); // 可能再次进入 rpc_call（嵌套调用）
    }
  }
  return result;
}

/* ---------------- 服务端 ---------------- */

int rpc_service_create(const char *name) {
  int rc = ipc_register(name);
  return rc == IPC_OK ? RPC_OK : rpc_map_ipc_error(rc);
}

int rpc_service_destroy(const char *name) {
  int rc = ipc_unregister(name);
  return rc == IPC_OK ? RPC_OK : rpc_map_ipc_error(rc);
}

int rpc_register_handler(unsigned opcode, rpc_handler_t handler) {
  if (!handler) {
    return RPC_ERR_INVAL;
  }
  for (int i = 0; i < RPC_MAX_HANDLER; i++) {
    if (rpc_handlers[i].used && rpc_handlers[i].opcode == opcode) {
      rpc_handlers[i].handler = handler;
      return RPC_OK;
    }
  }
  for (int i = 0; i < RPC_MAX_HANDLER; i++) {
    if (!rpc_handlers[i].used) {
      rpc_handlers[i].used = 1;
      rpc_handlers[i].opcode = opcode;
      rpc_handlers[i].handler = handler;
      return RPC_OK;
    }
  }
  return RPC_ERR_INVAL;
}

int rpc_unregister_handler(unsigned opcode) {
  for (int i = 0; i < RPC_MAX_HANDLER; i++) {
    if (rpc_handlers[i].used && rpc_handlers[i].opcode == opcode) {
      rpc_handlers[i].used = 0;
      return RPC_OK;
    }
  }
  return RPC_ERR_INVAL;
}

int rpc_serve_once(unsigned timeout_ms) {
  char rx[IPC_MAX_MSG_SIZE];
  ipc_msg_t got;
  int rc = ipc_recv_any(rx, sizeof(rx), &got, timeout_ms);
  if (rc < 0) {
    return rpc_map_ipc_error(rc);
  }
  if (got.size >= sizeof(rpc_wire_t)) {
    rpc_wire_t *wire = (rpc_wire_t *)rx;
    if (got.type == RPC_TYPE_REQUEST || got.type == RPC_TYPE_NOTIFY) {
      rpc_dispatch(&got, rx);
    } else if (got.type == RPC_TYPE_REPLY && rpc_is_outstanding(got.id)) {
      unsigned len = wire->len;
      if (len > got.size - sizeof(rpc_wire_t)) {
        len = got.size - sizeof(rpc_wire_t);
      }
      rpc_stash_put(got.id, wire->status, rx + sizeof(rpc_wire_t), len);
    }
  }
  return RPC_OK;
}

void rpc_stop(void) { rpc_stop_flag = 1; }

int rpc_serve_forever(void) {
  rpc_stop_flag = 0;
  while (!rpc_stop_flag) {
    int rc = rpc_serve_once(0);
    if (rc != RPC_OK) {
      return rc;
    }
  }
  return RPC_OK;
}

/* ---------------- 客户端 ---------------- */

int rpc_connect(const char *name, rpc_endpoint_t *ep, unsigned timeout_ms) {
  unsigned deadline = (unsigned)clock() + timeout_ms;
  if (!ep) {
    return RPC_ERR_INVAL;
  }
  for (;;) {
    unsigned generation = 0;
    int tid = ipc_lookup(name, &generation);
    if (tid >= 0) {
      ep->tid = (unsigned)tid;
      ep->generation = generation;
      return RPC_OK;
    }
    if (timeout_ms == 0 || (int)((unsigned)clock() - deadline) >= 0) {
      return RPC_ERR_NO_SERVICE;
    }
    api_yield();
  }
}

int rpc_call_tid(unsigned tid, unsigned generation, unsigned opcode,
                 const void *arg, unsigned arg_len, void *ret, unsigned ret_cap,
                 unsigned *ret_len, unsigned timeout_ms) {
  char tx[sizeof(rpc_wire_t) + RPC_MAX_PAYLOAD];
  unsigned call_id;
  int rc;

  if (arg_len > RPC_MAX_PAYLOAD) {
    return RPC_ERR_TOOBIG;
  }
  if (arg_len && !arg) {
    return RPC_ERR_INVAL;
  }
  if (rpc_depth >= RPC_MAX_NESTING) {
    return RPC_ERR_NESTING;
  }
  call_id = rpc_next_id++;
  if (rpc_next_id == RPC_KERNEL_CALL) {
    rpc_next_id = 1;
  }
  rpc_wire_t *wire = (rpc_wire_t *)tx;
  wire->opcode = opcode;
  wire->status = 0;
  wire->len = arg_len;
  if (arg_len) {
    memcpy(tx + sizeof(rpc_wire_t), arg, arg_len);
  }

  ipc_msg_t out;
  out.peer_tid = tid;
  out.peer_generation = generation;
  out.type = RPC_TYPE_REQUEST;
  out.id = call_id;
  out.size = sizeof(rpc_wire_t) + arg_len;
  out.flags = 0;
  out.timeout_ms = timeout_ms;
  out.from_filter = IPC_ANY_TID;
  out.data = tx;

  rpc_outstanding[rpc_depth++] = call_id;
  rc = ipc_send_msg(&out);
  if (rc != IPC_OK) {
    rpc_depth--;
    return rpc_map_ipc_error(rc);
  }
  rc = rpc_wait_reply(call_id, ret, ret_cap, ret_len, timeout_ms);
  rpc_depth--;
  return rc;
}

int rpc_call(rpc_endpoint_t *ep, unsigned opcode, const void *arg,
             unsigned arg_len, void *ret, unsigned ret_cap, unsigned *ret_len,
             unsigned timeout_ms) {
  if (!ep) {
    return RPC_ERR_INVAL;
  }
  return rpc_call_tid(ep->tid, ep->generation, opcode, arg, arg_len, ret,
                      ret_cap, ret_len, timeout_ms);
}

int rpc_notify(rpc_endpoint_t *ep, unsigned opcode, const void *arg,
               unsigned arg_len) {
  char tx[sizeof(rpc_wire_t) + RPC_MAX_PAYLOAD];
  int rc;

  if (!ep) {
    return RPC_ERR_INVAL;
  }
  if (arg_len > RPC_MAX_PAYLOAD) {
    return RPC_ERR_TOOBIG;
  }
  if (arg_len && !arg) {
    return RPC_ERR_INVAL;
  }
  rpc_wire_t *wire = (rpc_wire_t *)tx;
  wire->opcode = opcode;
  wire->status = 0;
  wire->len = arg_len;
  if (arg_len) {
    memcpy(tx + sizeof(rpc_wire_t), arg, arg_len);
  }

  ipc_msg_t out;
  out.peer_tid = ep->tid;
  out.peer_generation = ep->generation;
  out.type = RPC_TYPE_NOTIFY;
  // Notifications have no reply to correlate and can be sent by a worker
  // without touching the receiving thread's request state.
  out.id = 0;
  out.size = sizeof(rpc_wire_t) + arg_len;
  out.flags = IPC_NOWAIT | IPC_DELIVER_NOW;
  out.timeout_ms = 0;
  out.from_filter = IPC_ANY_TID;
  out.data = tx;
  rc = ipc_send_msg(&out);
  return rc == IPC_OK ? RPC_OK : rpc_map_ipc_error(rc);
}
