#ifndef PLOS_RPC_WIRE_H
#define PLOS_RPC_WIRE_H

#define RPC_TYPE_REQUEST 1
#define RPC_TYPE_REPLY 2
#define RPC_TYPE_NOTIFY 3

#define RPC_OK 0
#define RPC_ERR_INVAL -100      // 参数错误
#define RPC_ERR_NO_SERVICE -101 // 找不到服务
#define RPC_ERR_BAD_OPCODE -102 // 服务端没有这个函数号
#define RPC_ERR_TIMEOUT -103    // 等待应答超时
#define RPC_ERR_TOOBIG -104     // 参数或返回值太大
#define RPC_ERR_TRANSPORT -105 // 底层 IPC 出错（对方可能已经退出）
#define RPC_ERR_NESTING -106   // 嵌套调用太深
#define RPC_ERR_NOMEM -107     // 内存不足
#define RPC_ERR_BUSY -108      // 非阻塞通知的目标队列已满

// 处理函数返回这个值表示「故意不回复」（用于测试超时/单向通知）
#define RPC_NO_REPLY 0x7ffffffe

// 传输格式（跟在 IPC 负载最前面）
typedef struct {
  unsigned opcode; // 函数号
  int status;      // 应答里的返回状态
  unsigned len;    // 后面负载的字节数
} rpc_wire_t;

// 客户端持有的服务端点
typedef struct {
  unsigned tid;
  unsigned generation;
} rpc_endpoint_t;

// The high bit identifies kernel calls; user requests cannot set it.
#define RPC_KERNEL_CALL 0x80000000u

#if defined(__cplusplus)
static_assert(sizeof(rpc_wire_t) == 12, "RPC wire ABI");
#else
_Static_assert(sizeof(rpc_wire_t) == 12, "RPC wire ABI");
#endif

#endif
