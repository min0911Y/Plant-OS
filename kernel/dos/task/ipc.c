// 进程间通讯
// Copyright (C) zhouzhihao & min0911_ 2022
// 核心代码：zhouzhihao编写
// UPDATE: min0911_ : 增加了一个新的函数 可以用来修改发信人
// BUGFIX: min0911_ :
// GetIPCMessage函数 如果有多个消息 会返回最后一个消息 而不是最近发送的消息

#include <dos.h>
int send_ipc_message(int to_tid, void *data, unsigned int size, char type) {
  mtask *to_task = get_task(to_tid), *this_task = current_task();
  // int levelold = this_task->level;
  if (to_task->ipc_header.now == MAX_IPC_MESSAGE - 1) {
    irq_mask_clear(0);
    return -1;
  }

  IPCMessage msg;
  void *now_data = page_malloc(size);
  memcpy(now_data, data, size);

  msg.data = now_data;
  msg.size = size;
  msg.from_tid = this_task->tid;
  to_task->ipc_header.messages[to_task->ipc_header.now] = msg;
  to_task->ipc_header.now++;

  // 
  logk("send_ipc_message: msg: tid %d => tid %d\n", msg.from_tid, to_tid);

  int now = to_task->ipc_header.now;
  // task_run(to_task);
  if (type == synchronous) { // 同步
    // change_level(this_task, 3);
    while (to_task->ipc_header.now != now - 1)
      ;
    // change_level(this_task, levelold);
  }
  return 0;
}
int send_ipc_message_by_name(char *tname, void *data, unsigned int size,
                             char type) {
  // struct TASK *to_task = get_task_by_name(tname);
  // return send_ipc_message((to_task->sel / 8) - 103, data, size, type);
}
int get_ipc_message(void *data, int from_tid) {
  irq_mask_set(0);
  mtask *this_task = current_task();
  if (this_task->ipc_header.now == 0) {
    irq_mask_clear(0);
    return -1;
  }
  for (int i = MAX_IPC_MESSAGE - 1; i >= 0; i--) { // 优先取后发的
    IPCMessage msg = this_task->ipc_header.messages[i];
    // printk("TID-- %d / %d\n", this_task->ipc_header.from_tid[i],from_tid);
    if (msg.from_tid == from_tid) {
      logk("get_ipc_message: i = %d; from: tid %d\n",i, msg.from_tid);
      // printk("GetIPCMessage--%d now = \n", from_tid,
      // this_task->ipc_header.now);
      memcpy(data, msg.data, msg.size);
      page_free((void *)msg.data, msg.size);
      // printk("%08x\n",*(unsigned int*)data);
      if (i == MAX_IPC_MESSAGE - 1) {
        this_task->ipc_header.messages[i].data = 0;
        this_task->ipc_header.messages[i].size = 0;
        this_task->ipc_header.messages[i].from_tid = 0;
      } else {
        for (int j = i; j < MAX_IPC_MESSAGE - 1; j++) {
          logk("get_ipc_message: j = %d; from: tid %d\n",j,this_task->ipc_header.messages[j].from_tid);
          this_task->ipc_header.messages[j] = this_task->ipc_header.messages[j + 1];
        }
      }
      this_task->ipc_header.now--;
      irq_mask_clear(0);
      //  sleep(10);
      return 1;
    }
  }
  // printk("%08x\n",*(unsigned int *)data);
  irq_mask_clear(0);
  // sleep(10);
  return -1;
}
int get_ipc_message_by_tid(int tid, void *data, int from_tid) {
  irq_mask_set(0);
  mtask *this_task = get_task(tid);
  if (this_task->ipc_header.now == 0) {
    irq_mask_clear(0);
    return -1;
  }
  for (int i = MAX_IPC_MESSAGE - 1; i >= 0; i--) { // 优先取后发的
    IPCMessage msg = this_task->ipc_header.messages[i];
    // printk("TID-- %d / %d\n", this_task->ipc_header.from_tid[i],from_tid);
    if (msg.from_tid == from_tid) {
      // printk("GetIPCMessage--%d now = \n", from_tid,
      // this_task->ipc_header.now);
      memcpy(data, msg.data, msg.size);
      page_free((void *)msg.data, msg.size);
      // printk("%08x\n",*(unsigned int*)data);
      if (i == MAX_IPC_MESSAGE - 1) {
        this_task->ipc_header.messages[i].data = 0;
        this_task->ipc_header.messages[i].size = 0;
        this_task->ipc_header.messages[i].from_tid = 0;
      } else {
        for (int j = i; j != MAX_IPC_MESSAGE - 1; j++) {
          this_task->ipc_header.messages[j] = this_task->ipc_header.messages[j + 1];
        }
      }
      this_task->ipc_header.now--;
      irq_mask_clear(0);
      //  sleep(10);
      return 1;
    }
  }
  // printk("%08x\n",*(unsigned int *)data);
  irq_mask_clear(0);
  // sleep(10);
  return -1;
}
int get_ipc_message_by_name(void *data, char *tname) {
  // mtask *from_task = get_task_by_name(tname);
  // return get_ipc_message(data, (from_task->sel / 8) - 103);
}
int ipc_message_status() { return current_task()->ipc_header.now; }
unsigned int ipc_message_len(int from_tid) {
  // Maskirq(0);
  mtask *this_task = current_task();
  // printk("entry IPCMessageLength\n");
  for (int i = MAX_IPC_MESSAGE - 1; i >= 0; i--) {
    // printk("TID-- %d / %d\n", this_task->ipc_header.from_tid[i], from_tid);
		IPCMessage msg = this_task->ipc_header.messages[i];
    if (msg.from_tid == from_tid) {
      // printk("Find %d\n", i);
      // ClearMaskIrq(0);
      return msg.size;
    }
  }
  // printk("Not Find %d\n", from_tid);
  irq_mask_clear(0);
  return 0xFFFFFFFF;
}
int send_ipc_message_by_tid(int to_tid,        // 收信人
                            int y_tid,         // 发信人
                            void *data,        // 数据
                            unsigned int size, // 大小
                            char type          // 类型
) {
  irq_mask_set(0);
  mtask *to_task = get_task(to_tid), *this_task = current_task();
  // int levelold = this_task->level;
  if (to_task->ipc_header.now == MAX_IPC_MESSAGE - 1) {
    irq_mask_clear(0);
    return -1;
  }
  void *now_data = page_malloc(size);
  memcpy(now_data, data, size);
  IPCMessage msg;
  msg.data = now_data;
  msg.size = size;
  msg.from_tid = y_tid;
  to_task->ipc_header.messages[to_task->ipc_header.now] = msg;
  to_task->ipc_header.now++;
  int now = to_task->ipc_header.now;
  irq_mask_clear(0);
  // sleep(10);
  if (type == synchronous) { // 同步
    while (to_task->ipc_header.now != now - 1)
      ;
  }
  return 0;
}
bool have_msg() {
  mtask *this_task = current_task();
  for (int i = MAX_IPC_MESSAGE - 1; i >= 0; i--) {
    IPCMessage msg = this_task->ipc_header.messages[i];
    if (msg.from_tid != 0) {
      return msg.size;
    }
  }
  return false;
}
int get_msg_all(void *data) {
  // printk("entry getMsgAll\n");
  irq_mask_set(0);
  mtask *this_task = current_task();
  if (this_task->ipc_header.now == 0) {
    irq_mask_clear(0);
    return -1;
  }
  for (int i = MAX_IPC_MESSAGE - 1; i >= 0; i--) { // 优先取后发的
    IPCMessage msg = this_task->ipc_header.messages[i];
    if (msg.from_tid != 0) {
      memcpy(data, msg.data, msg.size);
      page_free((void *)msg.data, msg.size);
      if (i == MAX_IPC_MESSAGE - 1) {
        this_task->ipc_header.messages[i].data = 0;
        this_task->ipc_header.messages[i].size = 0;
        this_task->ipc_header.messages[i].from_tid = 0;
      } else {
        for (int j = i; j != MAX_IPC_MESSAGE - 1; j++) {
          this_task->ipc_header.messages[j]= this_task->ipc_header.messages[j + 1];
        }
      }
      this_task->ipc_header.now--;
      irq_mask_clear(0);
      //  sleep(10);
      return 1;
    }
  }
  // printk("%08x\n",*(unsigned int *)data);
  irq_mask_clear(0);
  // sleep(10);
  return -1;
}