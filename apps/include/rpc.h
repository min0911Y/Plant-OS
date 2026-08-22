// Plant OS 用户态 RPC（远程过程调用）库
//
// 传输层用的是内核的 IPC 消息队列（见 ipc.h）：
//   请求  : type = RPC_TYPE_REQUEST, id = 调用序号, 负载 = rpc_wire_t + 参数
//   应答  : type = RPC_TYPE_REPLY,   id = 同一个调用序号, 负载 = rpc_wire_t + 返回值
//
// 关键特性：等待应答的过程中仍然会处理别人发来的请求，
// 所以 A 调 B 的过程中 B 可以回头调 A（互相调用 / 可重入），不会死锁。
#ifndef _PLOS_RPC_H
#define _PLOS_RPC_H
#include <ipc.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RPC_TYPE_REQUEST 1
#define RPC_TYPE_REPLY 2

#define RPC_MAX_NESTING 8 // 最大嵌套调用深度
#define RPC_MAX_HANDLER 32
// 单次调用的参数/返回值上限
#define RPC_MAX_PAYLOAD (IPC_MAX_MSG_SIZE - 12)

#define RPC_OK 0
#define RPC_ERR_INVAL -100     // 参数错误
#define RPC_ERR_NO_SERVICE -101 // 找不到服务
#define RPC_ERR_BAD_OPCODE -102 // 服务端没有这个函数号
#define RPC_ERR_TIMEOUT -103    // 等待应答超时
#define RPC_ERR_TOOBIG -104     // 参数或返回值太大
#define RPC_ERR_TRANSPORT -105  // 底层 IPC 出错（对方可能已经退出）
#define RPC_ERR_NESTING -106    // 嵌套调用太深
#define RPC_ERR_NOMEM -107      // 内存不足

// 处理函数返回这个值表示「故意不回复」（用于测试超时/单向通知）
#define RPC_NO_REPLY 0x7ffffffe

// 传输格式（跟在 IPC 负载最前面）
typedef struct {
  unsigned opcode; // 函数号
  int status;      // 应答里的返回状态
  unsigned len;    // 后面负载的字节数
} rpc_wire_t;

// 服务端处理函数看到的上下文
typedef struct {
  unsigned opcode;            // 函数号
  unsigned caller_tid;        // 调用方 tid
  unsigned caller_generation; // 调用方世代号
  unsigned call_id;           // 调用序号
  const void *arg;            // 参数
  unsigned arg_len;           // 参数长度
  void *ret;                  // 返回值缓冲区（RPC_MAX_PAYLOAD 字节）
  unsigned ret_cap;           // 缓冲区容量
  unsigned ret_len;           // 处理函数负责填写实际返回长度
} rpc_call_t;

typedef int (*rpc_handler_t)(rpc_call_t *call);

// 客户端持有的服务端点
typedef struct {
  unsigned tid;
  unsigned generation;
} rpc_endpoint_t;

/* ---------------- 服务端 ---------------- */
// 注册服务名（其它进程用 rpc_connect 按名字找到你）
int rpc_service_create(const char *name);
int rpc_service_destroy(const char *name);
// 注册/注销函数号对应的处理函数
int rpc_register_handler(unsigned opcode, rpc_handler_t handler);
int rpc_unregister_handler(unsigned opcode);
// 处理一条请求；timeout_ms 为 0 表示一直等（真正睡眠，不占 CPU）
int rpc_serve_once(unsigned timeout_ms);
// 循环处理请求，直到处理函数里调用了 rpc_stop()
int rpc_serve_forever(void);
void rpc_stop(void);

/* ---------------- 客户端 ---------------- */
// 按服务名查找服务端（timeout_ms > 0 时会重试，等服务端起来）
int rpc_connect(const char *name, rpc_endpoint_t *ep, unsigned timeout_ms);
// 发起调用；ret_len 可以为 NULL
int rpc_call(rpc_endpoint_t *ep, unsigned opcode, const void *arg,
             unsigned arg_len, void *ret, unsigned ret_cap, unsigned *ret_len,
             unsigned timeout_ms);
// 直接对某个 tid 发起调用（服务端回调客户端时用得上）
int rpc_call_tid(unsigned tid, unsigned generation, unsigned opcode,
                 const void *arg, unsigned arg_len, void *ret, unsigned ret_cap,
                 unsigned *ret_len, unsigned timeout_ms);
// 单向通知：只发请求，不等应答
int rpc_notify(rpc_endpoint_t *ep, unsigned opcode, const void *arg,
               unsigned arg_len);

#ifdef __cplusplus
}
#endif
#endif
