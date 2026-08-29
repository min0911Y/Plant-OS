// Powerint DOS 386
// Copyright (C) 2021-2022 zhouzhihao & min0911
#include <dos.h>
uint32_t running_mode = POWERINTDOS;  // 运行模式
uint32_t Path_Addr;
unsigned char *font, *ascfont, *hzkfont;
unsigned char* IVT;

struct tty *now_tty() {
  extern struct List *tty_list;
  struct tty *n;
  for (int j = 1; list_get(j, tty_list) != 0; j++) {
    n = (struct tty *)list_get(j, tty_list)->val;
    if ((now_tty_TextMode(n) && running_mode == POWERINTDOS) ||
        (now_tty_HighTextMode(n) && running_mode == HIGHTEXTMODE)) {
      return n;
    }
  }
  return NULL;
}


void task_sr1() {
  // // 提供安全结束进程
  // extern int tasknum;
  // while (1) {
  // re:
  //   // printk("Wake UP.\n");
  //   if (get_running_task_num() == 1) {
  //     while (get_running_task_num() == 1) {
  //       // printk("1\n");
  //       asm volatile("hlt");
  //     }
  //   }
  //   for (int i = 1; i != tasknum + 1; i++) {
  //     struct TASK* task = get_task(i);
  //     if (task->running == 0) {  // 进程没有运行
  //       printk("system retention task 1: kill task %d * 8 %s.\n", task->sel / 8, task->name);
  //       __sub_task(task);
  //       goto re;
  //     }
  //   }
  //   task_sleep(current_task());
  // }
}
void com_input(char* ptr, int len) {
  int i;
  for (i = 0; i != len; i++) {
    char in = read_serial();
    if (in == '\r') {
      ptr[i] = 0;
      logk("\n");
      break;
    } else if (in == '\b') {
      if (i == 0) {
        i--;
        continue;
      }
      i--;
      ptr[i] = 0;
      i--;
      logk("\b");
      logk(" ");
      logk("\b");
      continue;
    }
    logk("%c", in);
    ptr[i] = in;
  }
}
void task_sr2() {
  // SleepTask(current_task());

  // for (;;) {
  // }
  // for (;;) {
  //   printk("Debug> ");
  //   char buf[150];
  //   com_input(buf, 150);
  //   printk("Recved Command:%s\n", buf);
  //   if (strcmp("show_all", buf) == 0) {
  //     for (int i = 1; get_task(i) != 0; i++) {
  //       printk("Task %s,Sleep=%d,%d lock=%d is_child=%d\n", get_task(i)->name,
  //              get_task(i)->sleep, get_task(i)->fifosleep, get_task(i)->lock,
  //              get_task(i)->is_child);
  //     }
  //   } else {
  //     printk("Bad Command\n");
  //   }
  //   // printk("Task Running.\n");
  // }
  // while (1) {
  //   if (IPCMessageStatus() != 0) {
  //     printk("Get Message.\n");
  //     int tid = current_task()->IPC_header.from_tid[0];
  //     int length = IPCMessageLength(tid);
  //     int* data = page_malloc(length);
  //     GetIPCMessage(data, tid);
  //     printk("will set_mode %dx%dx%d\n",data[0],data[1],data[2]);
  //     int result = _set_mode(data[0],data[1],data[2]);
  //     printk("Set OK.\n");
  //     unsigned int *r = data[3];
  //     *r = result;
  //   }
  // }
}
