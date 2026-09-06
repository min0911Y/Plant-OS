// Plant OS IPC / RPC 测试程序
//
// 用 fork() 起两个进程，父进程当客户端、子进程当服务端，
// 通过内核的消息队列 + 服务名注册做 RPC 调用，重点验证：
//   1. 服务名注册与查找（含服务进程退出后名字自动回收）
//   2. 普通 RPC 调用：整数、字符串、大负载
//   3. 互相调用：服务端在处理请求的过程中回头调用客户端（嵌套 / 可重入）
//   4. 错误处理：未知函数号、超时、目标进程不存在
//   5. 底层消息队列：先进先出、按发送者过滤、非阻塞读空队列
//
// 屏幕上打印中文结果，同时用 logkf 把结果写到串口，方便自动化检查。
#include <ipc.h>
#include <rpc.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <task.h>
#include <time.h>
#include <tty_rpc.h>

#define SERVICE_MATH "plos.math"
#define SERVICE_CLIENT "plos.client"

/* 服务端提供的函数号 */
#define OP_ADD 1
#define OP_UPPER 2
#define OP_ECHO 3
#define OP_SUM_VIA_CB 4
#define OP_SEND_RAW 5
#define OP_STATS 6
#define OP_SILENT 7
#define OP_SHUTDOWN 8
#define OP_LEGACY 9
/* 客户端提供给服务端回调的函数号 */
#define OP_SQUARE 100
#define OP_LOG 101

#define RAW_TYPE 0xd0d0
#define RAW_COUNT 5
#define RAW_BURST 24 /* 大于内核队列深度，用来验证发送方阻塞 */
#define ECHO_SIZE 2048
#define CALL_TIMEOUT 5000

typedef struct {
  int a;
  int b;
} add_arg_t;

static unsigned served;   /* 服务端处理过的请求数 */
static int server_stop;   /* 服务端退出标志 */
static int client_logs;   /* 客户端收到的回调日志条数 */
static int checks, fails; /* 客户端的检查计数 */

static void check(const char *name, int ok, int got, int expect) {
  checks++;
  if (!ok) {
    fails++;
  }
  printf("[%s] %s (got=%d expect=%d)\n", ok ? "PASS" : "FAIL", name, got,
         expect);
  logkf("RPCTEST %s %s got=%d expect=%d\n", ok ? "PASS" : "FAIL", name, got,
        expect);
}

/* ------------------------- 服务端 ------------------------- */

static int h_add(rpc_call_t *call) {
  add_arg_t arg;
  int result;
  if (call->arg_len != sizeof(arg)) {
    return RPC_ERR_INVAL;
  }
  memcpy(&arg, call->arg, sizeof(arg));
  result = arg.a + arg.b;
  memcpy(call->ret, &result, sizeof(result));
  call->ret_len = sizeof(result);
  served++;
  return RPC_OK;
}

static int h_upper(rpc_call_t *call) {
  const char *in = (const char *)call->arg;
  char *out = (char *)call->ret;
  if (call->arg_len > call->ret_cap) {
    return RPC_ERR_TOOBIG;
  }
  for (unsigned i = 0; i < call->arg_len; i++) {
    char ch = in[i];
    out[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
  }
  call->ret_len = call->arg_len;
  served++;
  return RPC_OK;
}

static int h_echo(rpc_call_t *call) {
  if (call->arg_len > call->ret_cap) {
    return RPC_ERR_TOOBIG;
  }
  memcpy(call->ret, call->arg, call->arg_len);
  call->ret_len = call->arg_len;
  served++;
  return RPC_OK;
}

/* 互相调用：处理这个请求时，服务端会两次回头调用客户端 */
static int h_sum_via_cb(rpc_call_t *call) {
  rpc_endpoint_t client;
  char text[64];
  int x, square = 0, lines = 0, result, rc, n;
  unsigned len = 0;

  if (call->arg_len != sizeof(int)) {
    return RPC_ERR_INVAL;
  }
  memcpy(&x, call->arg, sizeof(x));

  /* 第一次回调：直接用请求里带的调用方 tid，不需要查服务名 */
  rc = rpc_call_tid(call->caller_tid, call->caller_generation, OP_SQUARE, &x,
                    sizeof(x), &square, sizeof(square), &len, CALL_TIMEOUT);
  if (rc != RPC_OK) {
    logkf("RPCTEST server callback square failed rc=%d\n", rc);
    return rc;
  }
  logkf("RPCTEST server got square(%d)=%d from client\n", x, square);

  /* 第二次回调：走服务名查找，验证反向也能按名字找到对端 */
  if (rpc_connect(SERVICE_CLIENT, &client, 1000) == RPC_OK) {
    n = snprintf(text, sizeof(text), "square(%d)=%d", x, square);
    rc = rpc_call(&client, OP_LOG, text, (unsigned)n + 1, &lines, sizeof(lines),
                  &len, CALL_TIMEOUT);
    if (rc != RPC_OK) {
      logkf("RPCTEST server callback log failed rc=%d\n", rc);
      return rc;
    }
  } else {
    logkf("RPCTEST server cannot find %s\n", SERVICE_CLIENT);
    return RPC_ERR_NO_SERVICE;
  }

  result = square + x;
  memcpy(call->ret, &result, sizeof(result));
  call->ret_len = sizeof(result);
  served++;
  return RPC_OK;
}

/* 单向通知：连发几条原始消息给调用方，用来验证队列的先进先出 */
static int h_send_raw(rpc_call_t *call) {
  unsigned count;
  if (call->arg_len != sizeof(count)) {
    return RPC_NO_REPLY;
  }
  memcpy(&count, call->arg, sizeof(count));
  for (unsigned i = 0; i < count; i++) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "raw-%u", i);
    /* 队列满时这里会阻塞，等客户端取走消息（超时 3 秒） */
    if (ipc_send_to(call->caller_tid, RAW_TYPE, i, buf, (unsigned)len + 1,
                    3000) != IPC_OK) {
      logkf("RPCTEST server raw send %u failed\n", i);
      break;
    }
  }
  served++;
  return RPC_NO_REPLY; /* 不回应答 */
}

/* 用旧接口（SendMessage/GetMessage）给调用方发一条消息，验证兼容层 */
static int h_legacy(rpc_call_t *call) {
  static char text[] = "legacy-ipc";
  SendMessage((int)call->caller_tid, text, sizeof(text));
  served++;
  return RPC_NO_REPLY;
}

static int h_stats(rpc_call_t *call) {
  unsigned value = served;
  memcpy(call->ret, &value, sizeof(value));
  call->ret_len = sizeof(value);
  return RPC_OK;
}

static int h_silent(rpc_call_t *call) {
  served++;
  logkf("RPCTEST server drops call %u on purpose\n", call->call_id);
  return RPC_NO_REPLY; /* 故意不回，客户端应该超时 */
}

static int h_shutdown(rpc_call_t *call) {
  server_stop = 1;
  rpc_stop();
  served++;
  return RPC_OK;
}

static int server_main(void) {
  int rc = rpc_service_create(SERVICE_MATH);
  if (rc != RPC_OK) {
    logkf("RPCTEST server register failed rc=%d\n", rc);
    printf("[server] Register service failed rc=%d\n", rc);
    return 1;
  }
  rpc_register_handler(OP_ADD, h_add);
  rpc_register_handler(OP_UPPER, h_upper);
  rpc_register_handler(OP_ECHO, h_echo);
  rpc_register_handler(OP_SUM_VIA_CB, h_sum_via_cb);
  rpc_register_handler(OP_SEND_RAW, h_send_raw);
  rpc_register_handler(OP_STATS, h_stats);
  rpc_register_handler(OP_SILENT, h_silent);
  rpc_register_handler(OP_SHUTDOWN, h_shutdown);
  rpc_register_handler(OP_LEGACY, h_legacy);

  printf("[server] tid=%d, service \"%s\" ready\n", NowTaskID(), SERVICE_MATH);
  logkf("RPCTEST server ready tid=%d\n", NowTaskID());

  while (!server_stop) {
    /* timeout 传 0：真正睡下去，由内核在消息到达时唤醒 */
    rc = rpc_serve_once(0);
    if (rc != RPC_OK) {
      logkf("RPCTEST server serve error rc=%d\n", rc);
      break;
    }
  }
  rpc_service_destroy(SERVICE_MATH);
  printf("[server] Handled %u request(s), exit\n", served);
  logkf("RPCTEST server exit served=%u\n", served);
  return 0;
}

/* ------------------------- 客户端 ------------------------- */

static int h_square(rpc_call_t *call) {
  int x, result;
  if (call->arg_len != sizeof(x)) {
    return RPC_ERR_INVAL;
  }
  memcpy(&x, call->arg, sizeof(x));
  result = x * x;
  memcpy(call->ret, &result, sizeof(result));
  call->ret_len = sizeof(result);
  logkf("RPCTEST client handled square(%d) from tid=%u\n", x, call->caller_tid);
  return RPC_OK;
}

static int h_log(rpc_call_t *call) {
  client_logs++;
  printf("  <- [client] received callback log from server: %s\n", (const char *)call->arg);
  logkf("RPCTEST client log[%d] %s\n", client_logs, (const char *)call->arg);
  memcpy(call->ret, &client_logs, sizeof(client_logs));
  call->ret_len = sizeof(client_logs);
  return RPC_OK;
}

/* 让服务端连发 count 条原始消息，验证队列的先进先出；
   count 超过队列深度时还会顺带验证「队列满 -> 发送方阻塞 -> 取走后继续」 */
static int recv_raw_burst(rpc_endpoint_t *ep, unsigned server_tid,
                          unsigned count) {
  int ordered = 0;
  int rc = rpc_notify(ep, OP_SEND_RAW, &count, sizeof(count));
  if (rc != RPC_OK) {
    logkf("RPCTEST raw notify failed rc=%d\n", rc);
    return -1;
  }
  for (unsigned i = 0; i < count; i++) {
    char buf[64], want[32];
    ipc_msg_t info;
    int got = ipc_recv_from(server_tid, buf, sizeof(buf), &info, 5000);
    if (got < 0) {
      logkf("RPCTEST raw recv %u failed rc=%d\n", i, got);
      break;
    }
    snprintf(want, sizeof(want), "raw-%u", i);
    if (info.type == RAW_TYPE && info.id == i && strcmp(buf, want) == 0) {
      ordered++;
    } else {
      logkf("RPCTEST raw out of order: type=%x id=%u data=%s want=%s\n",
            info.type, info.id, buf, want);
    }
  }
  return ordered;
}

static void test_raw_queue(rpc_endpoint_t *ep, unsigned server_tid) {
  int ordered = recv_raw_burst(ep, server_tid, RAW_COUNT);
  check("raw_fifo_order", ordered == RAW_COUNT, ordered, RAW_COUNT);
  /* 一次发超过队列深度（MAX_IPC_MESSAGE=16）的消息 */
  ordered = recv_raw_burst(ep, server_tid, RAW_BURST);
  check("raw_queue_full_blocking", ordered == RAW_BURST, ordered, RAW_BURST);
}

static int client_main(unsigned server_tid) {
  rpc_endpoint_t ep;
  unsigned generation = 0, ret_len = 0;
  unsigned stats = 0;
  char *big, *echo_back;
  char upper[64];
  add_arg_t add_arg;
  int rc, value;

  rpc_service_create(SERVICE_CLIENT);
  rpc_register_handler(OP_SQUARE, h_square);
  rpc_register_handler(OP_LOG, h_log);
  printf("[client] tid=%d, server tid=%u\n", NowTaskID(), server_tid);
  logkf("RPCTEST client start tid=%d server_tid=%u\n", NowTaskID(), server_tid);

  /* 1. 不存在的服务名 */
  rc = ipc_lookup("no.such.service", &generation);
  check("lookup_missing", rc == IPC_ERR_NOTFOUND, rc, IPC_ERR_NOTFOUND);

  /* 2. 按名字连接服务端（等它注册好，最多 3 秒） */
  rc = rpc_connect(SERVICE_MATH, &ep, 3000);
  check("connect_service", rc == RPC_OK, rc, RPC_OK);
  if (rc != RPC_OK) {
    return fails;
  }
  check("connect_tid", ep.tid == server_tid, (int)ep.tid, (int)server_tid);

  /* 3. 整数调用 */
  add_arg.a = 3;
  add_arg.b = 4;
  value = 0;
  rc = rpc_call(&ep, OP_ADD, &add_arg, sizeof(add_arg), &value, sizeof(value),
                &ret_len, CALL_TIMEOUT);
  check("call_add", rc == RPC_OK && value == 7, value, 7);

  /* 4. 字符串调用 */
  memset(upper, 0, sizeof(upper));
  rc = rpc_call(&ep, OP_UPPER, "plant os rpc", 13, upper, sizeof(upper),
                &ret_len, CALL_TIMEOUT);
  check("call_upper", rc == RPC_OK && strcmp(upper, "PLANT OS RPC") == 0, rc,
        RPC_OK);

  /* 5. 大负载往返 */
  big = (char *)malloc(ECHO_SIZE);
  echo_back = (char *)malloc(ECHO_SIZE);
  if (big && echo_back) {
    int same = 1;
    for (int i = 0; i < ECHO_SIZE; i++) {
      big[i] = (char)(i * 7 + 1);
    }
    memset(echo_back, 0, ECHO_SIZE);
    rc = rpc_call(&ep, OP_ECHO, big, ECHO_SIZE, echo_back, ECHO_SIZE, &ret_len,
                  CALL_TIMEOUT);
    if (rc != RPC_OK || ret_len != ECHO_SIZE) {
      same = 0;
    } else {
      for (int i = 0; i < ECHO_SIZE; i++) {
        if (big[i] != echo_back[i]) {
          same = 0;
          break;
        }
      }
    }
    check("call_echo_2kb", same, (int)ret_len, ECHO_SIZE);
    free(big);
    free(echo_back);
  }

  /* 6. 互相调用：服务端处理请求时回调客户端两次 */
  value = 0;
  client_logs = 0;
  rc = rpc_call(&ep, OP_SUM_VIA_CB, (int[]){7}, sizeof(int), &value,
                sizeof(value), &ret_len, CALL_TIMEOUT);
  check("mutual_call_result", rc == RPC_OK && value == 56, value, 56);
  check("mutual_call_callback", client_logs == 1, client_logs, 1);

  /* 7. 未知函数号 */
  rc = rpc_call(&ep, 4242, NULL, 0, NULL, 0, &ret_len, CALL_TIMEOUT);
  check("bad_opcode", rc == RPC_ERR_BAD_OPCODE, rc, RPC_ERR_BAD_OPCODE);

  /* 8. 服务端故意不回应答 -> 客户端超时 */
  rc = rpc_call(&ep, OP_SILENT, NULL, 0, NULL, 0, &ret_len, 300);
  check("call_timeout", rc == RPC_ERR_TIMEOUT, rc, RPC_ERR_TIMEOUT);

  /* 9. 底层消息队列的先进先出 */
  test_raw_queue(&ep, server_tid);

  /* 10. 旧接口（SendMessage/GetMessage/MessageLength/haveMsg）兼容性 */
  {
    char buf[32];
    int len = -1;
    memset(buf, 0, sizeof(buf));
    rc = rpc_notify(&ep, OP_LEGACY, NULL, 0);
    for (int i = 0; i < 500 && !haveMsg(); i++) {
      api_yield();
    }
    len = (int)MessageLength((int)server_tid);
    GetMessage(buf, (int)server_tid);
    check("legacy_api", rc == RPC_OK && len == 11 &&
                            strcmp(buf, "legacy-ipc") == 0,
          len, 11);
  }

  /* 11. 队列空时非阻塞读 */
  {
    char buf[8];
    ipc_msg_t msg;
    msg.peer_tid = 0;
    msg.peer_generation = 0;
    msg.type = 0;
    msg.id = 0;
    msg.size = sizeof(buf);
    msg.flags = IPC_NOWAIT;
    msg.timeout_ms = 0;
    msg.from_filter = IPC_ANY_TID;
    msg.data = buf;
    rc = ipc_recv_msg(&msg);
    check("recv_nowait_empty", rc == IPC_ERR_EMPTY, rc, IPC_ERR_EMPTY);
    check("pending_zero", ipc_pending() == 0, ipc_pending(), 0);
  }

  /* 12. 发给不存在的进程 */
  rc = ipc_send_to(250, 1, 0, "x", 1, 0);
  check("send_to_dead_tid", rc == IPC_ERR_NOTASK, rc, IPC_ERR_NOTASK);

  /* 13. 负载超过上限 */
  rc = ipc_send_to(server_tid, 1, 0, upper, IPC_MAX_MSG_SIZE + 1, 0);
  check("send_too_big", rc == IPC_ERR_TOOBIG, rc, IPC_ERR_TOOBIG);

  /* 14. 让服务端报告它处理了多少请求 */
  rc = rpc_call(&ep, OP_STATS, NULL, 0, &stats, sizeof(stats), &ret_len,
                CALL_TIMEOUT);
  check("call_stats", rc == RPC_OK && stats >= 8, (int)stats, 8);

  /* 15. 关掉服务端 */
  rc = rpc_call(&ep, OP_SHUTDOWN, NULL, 0, NULL, 0, &ret_len, CALL_TIMEOUT);
  check("call_shutdown", rc == RPC_OK, rc, RPC_OK);

  /* 16. 服务端退出后，服务名应该被内核自动回收 */
  {
    int status = waittid(server_tid);
    check("server_exit_status", status == 0, status, 0);
    rc = ipc_lookup(SERVICE_MATH, &generation);
    check("service_auto_unregister", rc == IPC_ERR_NOTFOUND, rc,
          IPC_ERR_NOTFOUND);
    rc = rpc_call(&ep, OP_ADD, &add_arg, sizeof(add_arg), &value, sizeof(value),
                  &ret_len, 500);
    check("call_after_exit", rc == RPC_ERR_NO_SERVICE, rc, RPC_ERR_NO_SERVICE);
  }

  rpc_service_destroy(SERVICE_CLIENT);
  return fails;
}

/* Kernel TTY calls share the RPC wire protocol, but not the user inbox. */
#define OP_TTY_FINISH (TTY_RPC_DISPATCH + 1)
static const char tty_test_ansi[] = "\033[38;2;12;34;56m\033]0;raw\a";
static const char tty_test_native[] =
    "\033[?25l\033[?25h\033[38;2;12;34;56m\033]0;raw\a\033[0m";
static struct {
  tty_t handle;
  unsigned writes, bytes, reads;
  int failed, finished;
} tty_test;

static int tty_test_dispatch(rpc_call_t *call) {
  if (call->arg_len < sizeof(tty_rpc_request_t) ||
      call->ret_cap < sizeof(tty_rpc_reply_t) ||
      !(call->call_id & RPC_KERNEL_CALL))
    return RPC_ERR_INVAL;
  const tty_rpc_request_t *request = call->arg;
  tty_test.failed |= request->handle != tty_test.handle;
  tty_rpc_reply_t reply = {.state = request->state};
  switch (request->operation) {
  case TTY_RPC_WRITE: {
    unsigned length = call->arg_len - sizeof(*request);
    const char *text = (const char *)(request + 1);
    tty_test.writes++;
    if (length == 1 && text[0] == '!') {
      tty_test.bytes++;
      // Reply after the kernel deadline; it must never enter the user inbox.
      sleep(2500);
      break;
    }
    for (unsigned i = 0; i < length; i++) {
      unsigned offset = tty_test.bytes++;
      if (offset < 6000) {
        tty_test.failed |= text[i] != 'x';
        reply.state.x = (reply.state.x + 1) % 80;
      } else {
        offset -= 6000;
        tty_test.failed |= offset >= sizeof(tty_test_native) - 1 ||
                           text[i] != tty_test_native[offset];
      }
    }
    break;
  }
  case TTY_RPC_MOVE:
    reply.state.x = request->args.cursor.x;
    reply.state.y = request->args.cursor.y;
    break;
  case TTY_RPC_CLEAR:
    reply.state.x = reply.state.y = 0;
    break;
  case TTY_RPC_INPUT_STATUS:
    reply.value = 1;
    break;
  case TTY_RPC_INPUT_GET:
    tty_test.reads++;
    reply.value = tty_test.reads == 1 ? -1 : 42;
    // This wake arrives while the reader is still waiting for the empty reply.
    if (tty_test.reads == 1)
      tty_test.failed |= tty_notify_input(tty_test.handle) != 0;
    break;
  case TTY_RPC_DRAW_BOX:
  case TTY_RPC_SCROLL:
    break;
  default:
    return RPC_ERR_BAD_OPCODE;
  }
  memcpy(call->ret, &reply, sizeof(reply));
  call->ret_len = sizeof(reply);
  return RPC_OK;
}

static int tty_test_finish(rpc_call_t *call) {
  if (call->arg_len != sizeof(int))
    return RPC_ERR_INVAL;
  int failed;
  memcpy(&failed, call->arg, sizeof(failed));
  tty_test.failed |= failed;
  tty_test.finished = 1;
  return RPC_OK;
}

static int tty_test_writer(rpc_endpoint_t server) {
  int failed = 0;
  unsigned self = NowTaskID();
  ipc_msg_t message = {.peer_tid = self, .type = RAW_TYPE, .flags = IPC_NOWAIT};
  unsigned queued = 0;
  while (ipc_send_msg(&message) == IPC_OK)
    queued++;
  failed |= queued == 0;
  char text[6001];
  memset(text, 'x', sizeof(text) - 1);
  text[sizeof(text) - 1] = 0;
  print(text);
  failed |= ipc_pending() != (int)queued;
  for (unsigned i = 0; i < queued; i++) {
    ipc_msg_t got;
    failed |= ipc_recv_any(NULL, 0, &got, 500) < 0 || got.type != RAW_TYPE;
  }
  failed |= tty_get_xsize() != 80 || tty_get_ysize() != 25;
  goto_xy(7, 3);
  failed |= get_xy() != (7 << 16 | 3);
  tty_stop_cur_moving();
  goto_xy(5, 4);
  tty_start_cur_moving();
  failed |= get_xy() != (5 << 16 | 4);
  Text_Draw_Box(0, 0, 1, 1, 0x17);
  clear();
  failed |= get_xy() != 0;
  print(tty_test_ansi);
  for (const char *p = "\033[0m"; *p; p++)
    putch(*p);
  failed |= input_char_inSM() != 42;
  failed |=
      tty_notify_input(tty_test.handle) != -1; // Only the provider can wake it.
  failed |= tty_free(tty_test.handle) != -1;
  unsigned start = (unsigned)clock();
  putch('!');
  unsigned elapsed = (unsigned)clock() - start;
  failed |= elapsed < 1900 || elapsed > 5000;
  start = (unsigned)clock();
  putch('z'); // A disconnected TTY must not retry or block again.
  failed |= (unsigned)clock() - start >= 1000;
  int status = rpc_call(&server, OP_TTY_FINISH, &failed, sizeof(failed), NULL,
                        0, NULL, 5000);
  // The late kernel reply was discarded while this ordinary RPC was waiting.
  failed |= status != RPC_OK || ipc_pending() != 0;
  return failed;
}

static int tty_test_server(unsigned parent) {
  unsigned self = NowTaskID();
  rpc_endpoint_t server = {self, (unsigned)ipc_generation()};
  rpc_wire_t wire = {.opcode = TTY_RPC_DISPATCH};
  ipc_msg_t forged = {.peer_tid = self,
                      .peer_generation = server.generation,
                      .type = RPC_TYPE_REQUEST,
                      .id = RPC_KERNEL_CALL,
                      .data = &wire,
                      .size = sizeof(wire),
                      .flags = IPC_NOWAIT};
  if (ipc_send_msg(&forged) != IPC_ERR_INVAL ||
      tty_alloc(self, TTY_RPC_DISPATCH, 0, 25) != 0 ||
      tty_alloc(self, TTY_RPC_DISPATCH, 80, (unsigned)-1) != 0 ||
      rpc_register_handler(TTY_RPC_DISPATCH, tty_test_dispatch) != RPC_OK ||
      rpc_register_handler(OP_TTY_FINISH, tty_test_finish) != RPC_OK)
    return 1;
  tty_test.handle = tty_alloc(self, TTY_RPC_DISPATCH, 80, 25);
  if (tty_test.handle == 0 || tty_set(self, tty_test.handle) != 0)
    return 1;
  int writer = fork();
  if (writer == 0)
    _exit(tty_test_writer(server));
  tty_set(self, 0);
  if (writer < 0) {
    tty_free(tty_test.handle);
    return 1;
  }
  while (!tty_test.finished) {
    if (rpc_serve_once(5000) != RPC_OK) {
      tty_free(tty_test.handle);
      return 1;
    }
  }
  int status = waittid(writer);
  int failed = status != 0 || tty_test.failed || tty_test.writes != 10 ||
               tty_test.bytes != 6000 + sizeof(tty_test_native) ||
               tty_test.reads != 2;
  logkf("FARTTY RPC writes=%u bytes=%u reads=%u status=%d failed=%d\n",
        tty_test.writes, tty_test.bytes, tty_test.reads, status, failed);
  failed |= tty_free(tty_test.handle) != 0;
  failed |=
      tty_free(tty_test.handle) != -1; // Retired handles cannot be reused.
  // Exiting a provider retires its TTY and its still-blocked process.
  tty_test.handle = tty_alloc(self, TTY_RPC_DISPATCH, 80, 25);
  if (tty_test.handle == 0 || tty_set(self, tty_test.handle) != 0)
    return 1;
  int orphan = fork();
  if (orphan == 0) {
    for (;;)
      sleep(60000);
  }
  failed |= orphan < 0 || ipc_send_to(parent, RAW_TYPE, 0, &orphan,
                                      sizeof(orphan), 500) != IPC_OK;
  return failed;
}

static int test_tty_rpc(void) {
  unsigned parent = NowTaskID();
  int server = fork();
  if (server < 0)
    return 1;
  if (server == 0)
    _exit(tty_test_server(parent));
  int status = waittid(server);
  int orphan;
  ipc_msg_t got;
  if (status != 0 || ipc_recv_from(server, &orphan, sizeof(orphan), &got,
                                   500) != sizeof(orphan))
    return 1;
  unsigned deadline = (unsigned)clock() + 1000;
  for (;;) {
    task_info_t *tasks;
    size_t count;
    if (task_list(&tasks, &count) != 0)
      return 1;
    int alive = 0;
    for (size_t i = 0; i < count; i++)
      alive |= tasks[i].tid == (unsigned)orphan;
    free(tasks);
    if (!alive)
      return 0;
    if ((int)((unsigned)clock() - deadline) >= 0)
      return 1;
    sleep(10);
  }
}

int main(void) {
  int child;

  printf("==== Plant OS IPC/RPC test ====\n");
  logkf("RPCTEST begin\n");

  child = fork();
  if (child < 0) {
    printf("fork failed\n");
    logkf("RPCTEST FAIL fork\n");
    return 1;
  }
  if (child == 0) {
    return server_main();
  }

  fails = client_main((unsigned)child);
  int tty_status = test_tty_rpc();
  check("tty_rpc", tty_status == 0, tty_status, 0);
  printf("==== Result: %d/%d items passed ====\n", checks - fails, checks);
  logkf("RPCTEST done checks=%d fails=%d\n", checks, fails);
  return fails;
}
