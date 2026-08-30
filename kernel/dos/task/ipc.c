// 进程间通讯（消息队列 + 服务名注册）
// Copyright (C) zhouzhihao & min0911_ 2022
// 2026: 重写为「带世代号的先进先出消息队列 + 阻塞收发 + 服务名注册」，
//       用户态的 RPC 库（apps/libp/rpc.c）就建立在这套原语之上。
//
// 设计要点：
//  * 单核内核，临界区直接用关中断实现，不再借用 lock.c 里的锁，
//    这样收发消息的路径上不会再发生「拿着锁去调度」的情况。
//  * 负载放在按需扩展的内核堆里，底层 span 由 page_malloc 分配，位于所有
//    页目录都映射的低端地址，因此收发双方在各自的地址空间里都能访问。
//  * 每条消息带 seq，接收时取序号最小的匹配消息，保证先进先出；
//    带过滤条件（只收某个 tid）时顺序也不会乱。
//  * 阻塞是「置 WAITING + 由对端 task_run 唤醒」，带超时的等待退化为
//    让出时间片的轮询（内核里的 sleep() 本身也是这么做的）。

#include <dos.h>
#include <irq.h>

#define TASK_ID_NONE ((uint32_t)-1)
#define IPC_TICK_MS 10 /* 一个时钟节拍 10ms */

typedef struct {
  char name[IPC_NAME_MAX];
  uint32_t tid;
  uint32_t generation;
  int used;
} ipc_service_t;

static ipc_service_t ipc_services[IPC_MAX_SERVICE];

/* ------------------------------------------------------------------ */
/* 队列的基础操作                                                      */
/* ------------------------------------------------------------------ */

// 只清空队列结构，不释放负载。fork 出来的子进程用它抹掉从父进程复制过来的
// 槽位（那些负载归父进程所有，不能在这里释放）。
void ipc_header_init(IPC_Header *ipc) {
  if (!ipc) {
    return;
  }
  ipc->count = 0;
  ipc->seq = 0;
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    ipc->messages[i].used = 0;
    ipc->messages[i].data = NULL;
    ipc->messages[i].size = 0;
    ipc->messages[i].from_tid = TASK_ID_NONE;
    ipc->messages[i].from_generation = 0;
    ipc->messages[i].type = 0;
    ipc->messages[i].id = 0;
    ipc->messages[i].seq = 0;
  }
}

// 任务槽初始化（不释放任何内存）
void ipc_task_init(mtask *task) {
  if (!task) {
    return;
  }
  ipc_header_init(&task->ipc_header);
  task->ipc_wait_peer = TASK_ID_NONE;
  task->ipc_deadline = 0;
  task->ipc_deadline_set = 0;
}

static void ipc_drop_message(IPC_Header *ipc, IPCMessage *msg) {
  if (msg->data) {
    free(msg->data);
    msg->data = NULL;
  }
  msg->used = 0;
  msg->size = 0;
  msg->from_tid = TASK_ID_NONE;
  msg->from_generation = 0;
  msg->type = 0;
  msg->id = 0;
  msg->seq = 0;
  if (ipc->count) {
    ipc->count--;
  }
}

// 找出队列里序号最小（也就是最早入队）的匹配消息
static IPCMessage *ipc_find_oldest(IPC_Header *ipc, uint32_t from_filter) {
  IPCMessage *found = NULL;
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    IPCMessage *msg = &ipc->messages[i];
    if (!msg->used) {
      continue;
    }
    if (from_filter != IPC_ANY_TID && msg->from_tid != from_filter) {
      continue;
    }
    if (!found || msg->seq < found->seq) {
      found = msg;
    }
  }
  return found;
}

static IPCMessage *ipc_find_free(IPC_Header *ipc) {
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    if (!ipc->messages[i].used) {
      return &ipc->messages[i];
    }
  }
  return NULL;
}

// 唤醒所有因为「目标队列满」而阻塞在 to_tid 上的发送者
static void ipc_wake_senders(uint32_t to_tid) {
  task_iterator_t iterator = {0};
  mtask *task;
  while ((task = task_iter_next(&iterator)) != NULL) {
    if (task->state != WAITING || task->wait_reason != WAIT_REASON_IPC) {
      continue;
    }
    if (task->ipc_wait_peer == to_tid) {
      task_run(task);
    }
  }
}

static bool ipc_wake_receiver(mtask *task) {
  if (task->state == WAITING && task->wait_reason == WAIT_REASON_IPC &&
      task->ipc_wait_peer == TASK_ID_NONE) {
    task_run(task);
    return true;
  }
  return false;
}

// 任务退出时的清理：丢掉自己没读完的消息、注销服务名、放走等它收信的发送者。
// 已经投递给别人的消息不会被撤回（它们是独立的副本），对方读到时可以通过
// from_generation 判断发送者是否还活着。
void ipc_task_cleanup(mtask *task) {
  if (!task) {
    return;
  }
  IPC_Header *ipc = &task->ipc_header;
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    if (ipc->messages[i].used) {
      ipc_drop_message(ipc, &ipc->messages[i]);
    }
  }
  ipc_header_init(ipc);
  for (int i = 0; i < IPC_MAX_SERVICE; i++) {
    if (ipc_services[i].used && ipc_services[i].tid == task->tid &&
        ipc_services[i].generation == task->generation) {
      ipc_services[i].used = 0;
      ipc_services[i].name[0] = '\0';
    }
  }
  task->ipc_wait_peer = TASK_ID_NONE;
  ipc_wake_senders(task->tid);
}

/* ------------------------------------------------------------------ */
/* 等待辅助                                                            */
/* ------------------------------------------------------------------ */

static uint32_t ipc_deadline(uint32_t timeout_ms) {
  uint32_t ticks = (timeout_ms + IPC_TICK_MS - 1) / IPC_TICK_MS;
  if (ticks == 0) {
    ticks = 1;
  }
  return timerctl.count + ticks;
}

static bool ipc_deadline_passed(uint32_t deadline) {
  return (int32_t)(timerctl.count - deadline) >= 0;
}

// 让出 CPU 等待事件（有消息到达、队列腾出位置，或者等到超时）。
// 调用前必须处于关中断状态，返回时恢复调用者原中断状态。
//
// 注意：task_next() / arch_task_switch() 不可重入，如果开着中断去切换，
// 时钟中断可能正好落在切换过程中再调一次 task_next，把任务的内核栈指针搞乱
// （表现为返回用户态时 eip 变成随机值）。所以这里一直关着中断切换，
// 切回来之后再恢复原中断状态。
static void ipc_wait(mtask *self, uint32_t peer_tid, uint32_t deadline,
                     int use_deadline, irq_state_t state) {
  if (self->ready) {
    // 已经有人唤醒过我们了，直接回去重新检查
    self->ready = 0;
    irq_restore(state);
    return;
  }
  self->ipc_wait_peer = peer_tid;
  self->ipc_deadline = deadline;
  self->ipc_deadline_set = use_deadline ? 1 : 0;
  self->state = WAITING;
  self->wait_reason = WAIT_REASON_IPC;
  task_next();
  self->ipc_wait_peer = TASK_ID_NONE;
  self->ipc_deadline_set = 0;
  if (self->wait_reason == WAIT_REASON_IPC) {
    self->wait_reason = WAIT_REASON_NONE;
  }
  irq_restore(state);
}

// 由时钟中断调用：把等到超时的 IPC 等待者唤醒
void ipc_tick(void) {
  task_iterator_t iterator = {0};
  mtask *task;
  while ((task = task_iter_next(&iterator)) != NULL) {
    if (task->state != WAITING || task->wait_reason != WAIT_REASON_IPC ||
        !task->ipc_deadline_set) {
      continue;
    }
    if ((int32_t)(timerctl.count - task->ipc_deadline) >= 0) {
      task_run(task);
    }
  }
}

/* ------------------------------------------------------------------ */
/* 发送 / 接收                                                         */
/* ------------------------------------------------------------------ */

// 向 to_tid 投递一条消息。
// to_generation 非 0 时会校验目标的世代号，避免 tid 复用后投错进程。
// 返回 IPC_OK 或负的错误码。
int ipc_send(uint32_t to_tid, uint32_t to_generation, uint32_t type, uint32_t id,
             const void *data, uint32_t size, uint32_t flags,
             uint32_t timeout_ms) {
  mtask *self = current_task();
  void *payload = NULL;
  uint32_t deadline = 0;
  int result;

  if (size > IPC_MAX_MSG_SIZE) {
    return IPC_ERR_TOOBIG;
  }
  if (size && !data) {
    return IPC_ERR_INVAL;
  }
  if (size) {
    payload = malloc(size);
    if (!payload) {
      return IPC_ERR_NOMEM;
    }
    // 还在发送者的地址空间里，可以直接拷用户数据
    memcpy(payload, data, size);
  }
  if (timeout_ms) {
    deadline = ipc_deadline(timeout_ms);
  }

  for (;;) {
    irq_state_t state = irq_save();
    mtask *to = get_task(to_tid);
    if (!to || to->state == DIED ||
        (to_generation && to->generation != to_generation)) {
      irq_restore(state);
      result = IPC_ERR_NOTASK;
      break;
    }
    IPC_Header *ipc = &to->ipc_header;
    IPCMessage *slot = ipc_find_free(ipc);
    if (slot) {
      slot->data = payload;
      slot->size = size;
      slot->from_tid = self->tid;
      slot->from_generation = self->generation;
      slot->type = type;
      slot->id = id;
      slot->seq = ipc->seq++;
      slot->used = 1;
      ipc->count++;
      bool receiver_woken = ipc_wake_receiver(to);
      if ((flags & IPC_DELIVER_NOW) && receiver_woken) {
        /* The caller explicitly requests low latency.  task_next() runs with
         * the same saved interrupt state as the queue update, so the receiver
         * observes the message before this sender resumes. */
        mtask_run_now(to);
        task_next();
      }
      irq_restore(state);
      return IPC_OK;
    }
    if (flags & IPC_NOWAIT) {
      irq_restore(state);
      result = IPC_ERR_FULL;
      break;
    }
    if (timeout_ms && ipc_deadline_passed(deadline)) {
      irq_restore(state);
      result = IPC_ERR_TIMEOUT;
      break;
    }
    // 队列满：等目标取走消息后再试，切回后恢复原中断状态。
    ipc_wait(self, to_tid, deadline, timeout_ms != 0, state);
  }

  if (payload) {
    free(payload);
  }
  return result;
}

// 取出一条消息。bufsize 是 buf 的容量，负载超出容量时返回 IPC_ERR_TOOBIG，
// 消息会保留在队列里（info 里能看到真实长度）。
// 成功时返回实际拷贝的字节数（>= 0）。
int ipc_recv(void *buf, uint32_t bufsize, ipc_msg_info_t *info,
             uint32_t from_filter, uint32_t flags, uint32_t timeout_ms) {
  mtask *self = current_task();
  IPC_Header *ipc = &self->ipc_header;
  uint32_t deadline = 0;

  if (timeout_ms) {
    deadline = ipc_deadline(timeout_ms);
  }
  for (;;) {
    irq_state_t state = irq_save();
    IPCMessage *msg = ipc_find_oldest(ipc, from_filter);
    if (msg) {
      uint32_t size = msg->size;
      if (info) {
        info->from_tid = msg->from_tid;
        info->from_generation = msg->from_generation;
        info->type = msg->type;
        info->id = msg->id;
        info->size = size;
      }
      if (size > bufsize) {
        irq_restore(state);
        return IPC_ERR_TOOBIG;
      }
      if (size && buf) {
        memcpy(buf, msg->data, size);
      }
      ipc_drop_message(ipc, msg);
      ipc_wake_senders(self->tid);
      irq_restore(state);
      return (int)size;
    }
    if (flags & IPC_NOWAIT) {
      irq_restore(state);
      return IPC_ERR_EMPTY;
    }
    if (timeout_ms && ipc_deadline_passed(deadline)) {
      irq_restore(state);
      return IPC_ERR_TIMEOUT;
    }
    ipc_wait(self, TASK_ID_NONE, deadline, timeout_ms != 0, state);
  }
}

// 查看下一条消息的信息但不取走
int ipc_peek(ipc_msg_info_t *info, uint32_t from_filter) {
  irq_state_t state = irq_save();
  IPCMessage *msg = ipc_find_oldest(&current_task()->ipc_header, from_filter);
  if (!msg) {
    irq_restore(state);
    return IPC_ERR_EMPTY;
  }
  if (info) {
    info->from_tid = msg->from_tid;
    info->from_generation = msg->from_generation;
    info->type = msg->type;
    info->id = msg->id;
    info->size = msg->size;
  }
  irq_restore(state);
  return IPC_OK;
}

// 当前任务队列里的消息数
int ipc_pending(void) {
  irq_state_t state = irq_save();
  int count = (int)current_task()->ipc_header.count;
  irq_restore(state);
  return count;
}

/* ------------------------------------------------------------------ */
/* 服务名注册                                                          */
/* ------------------------------------------------------------------ */

static int ipc_name_valid(const char *name) {
  if (!name || !name[0]) {
    return 0;
  }
  for (int i = 0; i < IPC_NAME_MAX; i++) {
    if (name[i] == '\0') {
      return 1;
    }
  }
  return 0; // 太长
}

static void ipc_name_copy(char *dest, const char *src) {
  int i = 0;
  for (; i < IPC_NAME_MAX - 1 && src[i]; i++) {
    dest[i] = src[i];
  }
  dest[i] = '\0';
}

// 把当前任务注册成名字为 name 的服务
int ipc_service_register(const char *name) {
  if (!ipc_name_valid(name)) {
    return IPC_ERR_INVAL;
  }
  mtask *self = current_task();
  irq_state_t state = irq_save();
  int free_index = -1;
  for (int i = 0; i < IPC_MAX_SERVICE; i++) {
    if (!ipc_services[i].used) {
      if (free_index < 0) {
        free_index = i;
      }
      continue;
    }
    if (strcmp(ipc_services[i].name, name) == 0) {
      mtask *owner = get_task(ipc_services[i].tid);
      if (owner && owner->generation == ipc_services[i].generation &&
          owner->state != DIED) {
        irq_restore(state);
        return owner == self ? IPC_OK : IPC_ERR_EXIST;
      }
      // 原来的服务进程已经不在了，回收这个名字
      free_index = i;
      ipc_services[i].used = 0;
      break;
    }
  }
  if (free_index < 0) {
    irq_restore(state);
    return IPC_ERR_FULL;
  }
  ipc_name_copy(ipc_services[free_index].name, name);
  ipc_services[free_index].tid = self->tid;
  ipc_services[free_index].generation = self->generation;
  ipc_services[free_index].used = 1;
  irq_restore(state);
  return IPC_OK;
}

int ipc_service_unregister(const char *name) {
  if (!ipc_name_valid(name)) {
    return IPC_ERR_INVAL;
  }
  mtask *self = current_task();
  irq_state_t state = irq_save();
  for (int i = 0; i < IPC_MAX_SERVICE; i++) {
    if (!ipc_services[i].used || strcmp(ipc_services[i].name, name) != 0) {
      continue;
    }
    if (ipc_services[i].tid != self->tid) {
      irq_restore(state);
      return IPC_ERR_INVAL; // 只能注销自己注册的名字
    }
    ipc_services[i].used = 0;
    ipc_services[i].name[0] = '\0';
    irq_restore(state);
    return IPC_OK;
  }
  irq_restore(state);
  return IPC_ERR_NOTFOUND;
}

// 查询服务名对应的 tid，generation 非空时同时输出世代号
int ipc_service_lookup(const char *name, uint32_t *generation) {
  if (!ipc_name_valid(name)) {
    return IPC_ERR_INVAL;
  }
  irq_state_t state = irq_save();
  for (int i = 0; i < IPC_MAX_SERVICE; i++) {
    if (!ipc_services[i].used || strcmp(ipc_services[i].name, name) != 0) {
      continue;
    }
    mtask *owner = get_task(ipc_services[i].tid);
    if (!owner || owner->generation != ipc_services[i].generation ||
        owner->state == DIED) {
      ipc_services[i].used = 0;
      ipc_services[i].name[0] = '\0';
      break;
    }
    int tid = (int)ipc_services[i].tid;
    if (generation) {
      *generation = ipc_services[i].generation;
    }
    irq_restore(state);
    return tid;
  }
  irq_restore(state);
  return IPC_ERR_NOTFOUND;
}

/* ------------------------------------------------------------------ */
/* 兼容旧接口                                                          */
/* ------------------------------------------------------------------ */

// 旧接口：type == synchronous 时等到对方把消息取走再返回
int send_ipc_message(int to_tid, void *data, unsigned int size, char type) {
  if (to_tid < 0) {
    return -1;
  }
  int result = ipc_send((uint32_t)to_tid, 0, (uint32_t)type, 0, data, size, 0, 0);
  if (result != IPC_OK) {
    return -1;
  }
  if (type == synchronous) {
    // 等目标把队列清空到不含我们这条消息为止
    for (;;) {
      irq_state_t state = irq_save();
      mtask *to = get_task((uint32_t)to_tid);
      if (!to) {
        irq_restore(state);
        return 0;
      }
      IPCMessage *mine = NULL;
      for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
        IPCMessage *msg = &to->ipc_header.messages[i];
        if (msg->used && msg->from_tid == current_task()->tid) {
          mine = msg;
          break;
        }
      }
      if (!mine) {
        irq_restore(state);
        return 0;
      }
      /* 关中断切换，理由同 ipc_wait */
      task_next();
      irq_restore(state);
    }
  }
  return 0;
}

int send_ipc_message_by_name(char *tname, void *data, unsigned int size,
                             char type) {
  int tid = ipc_service_lookup(tname, NULL);
  if (tid < 0) {
    return -1;
  }
  return send_ipc_message(tid, data, size, type);
}

// 旧接口：阻塞地取一条来自 from_tid 的消息，调用者必须保证缓冲区足够大
int get_ipc_message(void *data, int from_tid) {
  ipc_msg_info_t info;
  int result = ipc_recv(data, IPC_MAX_MSG_SIZE, &info,
                        from_tid < 0 ? IPC_ANY_TID : (uint32_t)from_tid,
                        IPC_NOWAIT, 0);
  return result < 0 ? -1 : 0;
}

int get_ipc_message_by_name(void *data, char *tname) {
  int tid = ipc_service_lookup(tname, NULL);
  if (tid < 0) {
    return -1;
  }
  return get_ipc_message(data, tid);
}

int ipc_message_status() { return ipc_pending(); }

unsigned int ipc_message_len(int from_tid) {
  ipc_msg_info_t info;
  if (ipc_peek(&info, from_tid < 0 ? IPC_ANY_TID : (uint32_t)from_tid) !=
      IPC_OK) {
    return (unsigned int)-1;
  }
  return info.size;
}

bool have_msg() { return ipc_pending() != 0; }

// 旧接口：取任意一条消息
int get_msg_all(void *data) {
  ipc_msg_info_t info;
  int result = ipc_recv(data, IPC_MAX_MSG_SIZE, &info, IPC_ANY_TID, IPC_NOWAIT, 0);
  return result < 0 ? -1 : 0;
}
