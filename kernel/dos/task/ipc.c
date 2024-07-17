// 进程间通讯
// Copyright (C) zhouzhihao & min0911_ 2022
// 核心代码：zhouzhihao编写
// UPDATE: min0911_ : 增加了一个新的函数 可以用来修改发信人
// BUGFIX: min0911_ :
// GetIPCMessage函数 如果有多个消息 会返回最后一个消息 而不是最近发送的消息

#include <dos.h>
char dat[255];
int  send_ipc_message(int to_tid, void *data, unsigned int size, char type) {
  lock(&(get_task(to_tid)->ipc_header.l));
  IPC_Header *ipc     = &(get_task(to_tid)->ipc_header);
  IPCMessage *ipc_msg = NULL;
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    if (ipc->messages[i].flag1 + ipc->messages[i].flag2 == 0) {
      ipc_msg = &(ipc->messages[i]);
      break;
    }
  }
  if (!ipc_msg) {
    unlock(&(get_task(to_tid)->ipc_header.l));
    return -1;
  }
  ipc_msg->size = size;
  ipc_msg->data = (void *)page_malloc(size);
  memcpy(ipc_msg->data, data, size);
  ipc_msg->flag1    = 1;
  ipc_msg->from_tid = get_tid(current_task());
  if (type == synchronous) {
    ipc_msg->flag2 = 1;
  } else {
    ipc_msg->flag2 = 0;
  }
  ipc->now++;
  unlock(&(get_task(to_tid)->ipc_header.l));
  if (type == synchronous) {
    while (ipc_msg->flag1)
      ;
    lock(&(get_task(to_tid)->ipc_header.l));
    ipc_msg->flag2 = 0;
    unlock(&(get_task(to_tid)->ipc_header.l));
  }
  return 0;
}
int send_ipc_message_by_name(char *tname, void *data, unsigned int size, char type) {
  // struct TASK *to_task = get_task_by_name(tname);
  // return send_ipc_message((to_task->sel / 8) - 103, data, size, type);
}

int get_ipc_message(void *data, int from_tid) {
  lock(&(current_task()->ipc_header.l));
  if (current_task()->ipc_header.now == 0) {
    unlock(&(current_task()->ipc_header.l));
    return -1;
  }
  IPC_Header *ipc     = &(current_task()->ipc_header);
  IPCMessage *ipc_msg = NULL;
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    if (ipc->messages[i].flag1 == 1 && ipc->messages[i].from_tid == from_tid) {
      ipc_msg = &(ipc->messages[i]);
    }
  }
  if (!ipc_msg) {
    unlock(&(current_task()->ipc_header.l));
    return -1;
  }
  memcpy(data, ipc_msg->data, ipc_msg->size);
  page_free(ipc_msg->data, ipc_msg->size);
  ipc_msg->flag1 = 0;
  ipc->now--;
  unlock(&(current_task()->ipc_header.l));
  return 0;
}
int get_ipc_message_by_name(void *data, char *tname) {
  // mtask *from_task = get_task_by_name(tname);
  // return get_ipc_message(data, (from_task->sel / 8) - 103);
}
int ipc_message_status() {
  return 1;
}
unsigned int ipc_message_len(int from_tid) {
  lock(&(current_task()->ipc_header.l));
  if (current_task()->ipc_header.now == 0) {
    unlock(&(current_task()->ipc_header.l));
    return -1;
  }
  IPC_Header *ipc     = &(current_task()->ipc_header);
  IPCMessage *ipc_msg = NULL;
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    if (ipc->messages[i].flag1 == 1 && ipc->messages[i].from_tid == from_tid) {
      ipc_msg = &(ipc->messages[i]);
    }
  }
  if (!ipc_msg) {
    unlock(&(current_task()->ipc_header.l));
    return -1;
  }
  unlock(&(current_task()->ipc_header.l));
  return ipc_msg->size;
}
// 废弃
// int send_ipc_message_by_tid(int to_tid,        // 收信人
//                             int y_tid,         // 发信人
//                             void *data,        // 数据
//                             unsigned int size, // 大小
//                             char type          // 类型
// ) {
//   irq_mask_set(0);
//   mtask *to_task = get_task(to_tid), *this_task = current_task();
//   // int levelold = this_task->level;
//   if (to_task->ipc_header.now == MAX_IPC_MESSAGE - 1) {
//     irq_mask_clear(0);
//     return -1;
//   }
//   void *now_data = page_malloc(size);
//   memcpy(now_data, data, size);
//   IPCMessage msg;
//   msg.data = now_data;
//   msg.size = size;
//   msg.from_tid = y_tid;
//   to_task->ipc_header.messages[to_task->ipc_header.now] = msg;
//   to_task->ipc_header.now++;
//   int now = to_task->ipc_header.now;
//   irq_mask_clear(0);
//   // sleep(10);
//   if (type == synchronous) { // 同步
//     while (to_task->ipc_header.now != now - 1)
//       ;
//   }
//   return 0;
// }
bool have_msg() {
  lock(&(current_task()->ipc_header.l));
  IPC_Header *ipc     = &(current_task()->ipc_header);
  IPCMessage *ipc_msg = NULL;
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    if (ipc->messages[i].flag1 == 1) { ipc_msg = &(ipc->messages[i]); }
  }
  if (!ipc_msg) {
    unlock(&(current_task()->ipc_header.l));
    return false;
  }
  unlock(&(current_task()->ipc_header.l));
  return true;
}
int get_msg_all(void *data) {
  lock(&(current_task()->ipc_header.l));
  IPC_Header *ipc     = &(current_task()->ipc_header);
  IPCMessage *ipc_msg = NULL;
  for (int i = 0; i < MAX_IPC_MESSAGE; i++) {
    if (ipc->messages[i].flag1 == 1) { ipc_msg = &(ipc->messages[i]); }
  }
  if (!ipc_msg) {
    unlock(&(current_task()->ipc_header.l));
    return -1;
  }
  memcpy(data, ipc_msg->data, ipc_msg->size);
  page_free(ipc_msg->data, ipc_msg->size);
  ipc_msg->flag1 = 0;
  unlock(&(current_task()->ipc_header.l));
  while (ipc_msg->flag2)
    ;
  return 0;
}