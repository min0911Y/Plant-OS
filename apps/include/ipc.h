// Plant OS 进程间通讯（IPC）用户态接口
// 内核实现见 kernel/dos/task/ipc.c，系统调用号 int 36h / eax = 0x5d
#ifndef _PLOS_IPC_H
#define _PLOS_IPC_H
#ifdef __cplusplus
extern "C" {
#endif

#define IPC_MAX_MSG_SIZE 4096      // 单条消息负载上限
#define IPC_NAME_MAX 32            // 服务名最大长度（含结尾 '\0'）
#define IPC_NOWAIT 0x01            // 不阻塞
#define IPC_ANY_TID ((unsigned)-1) // 接收任意发送者的消息

#define IPC_OK 0
#define IPC_ERR_INVAL -1    // 参数错误
#define IPC_ERR_NOTASK -2   // 目标进程不存在
#define IPC_ERR_FULL -3     // 目标队列已满 / 服务名表已满
#define IPC_ERR_EMPTY -4    // 没有消息
#define IPC_ERR_TOOBIG -5   // 负载太大（或接收缓冲区太小）
#define IPC_ERR_EXIST -6    // 服务名已被占用
#define IPC_ERR_NOMEM -7    // 内核内存不足
#define IPC_ERR_TIMEOUT -8  // 超时
#define IPC_ERR_NOTFOUND -9 // 服务名不存在

// 与内核 ipc_user_msg_t 布局完全一致，改动时两边要一起改
typedef struct {
  unsigned peer_tid;        // 发送：目标 tid；接收：输出发送者 tid
  unsigned peer_generation; // 发送：0 表示不校验；接收：输出发送者世代号
  unsigned type;            // 消息类型（自定义）
  unsigned id;              // 关联号（自定义）
  unsigned size;            // 发送：负载长度；接收：入参为容量，出参为实际长度
  unsigned flags;           // IPC_NOWAIT
  unsigned timeout_ms;      // 0 表示一直等
  unsigned from_filter;     // 接收：只收该 tid 的消息，IPC_ANY_TID 表示都收
  void *data;               // 负载缓冲区
} ipc_msg_t;

// 原始接口
int ipc_send_msg(ipc_msg_t *msg);
int ipc_recv_msg(ipc_msg_t *msg);
int ipc_peek_msg(ipc_msg_t *msg);
int ipc_pending(void);
int ipc_register(const char *name);
int ipc_unregister(const char *name);
int ipc_lookup(const char *name, unsigned *generation);
int ipc_generation(void);

// 常用封装
int ipc_send_to(unsigned tid, unsigned type, unsigned id, const void *data,
                unsigned size, unsigned timeout_ms);
int ipc_recv_any(void *buf, unsigned bufsize, ipc_msg_t *out,
                 unsigned timeout_ms);
int ipc_recv_from(unsigned from_tid, void *buf, unsigned bufsize,
                  ipc_msg_t *out, unsigned timeout_ms);

#ifdef __cplusplus
}
#endif
#endif
