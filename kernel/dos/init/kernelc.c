// Powerint DOS 386
// Copyright (C) 2021-2022 zhouzhihao & min0911
#include <dos.h>
#include <mst.h>
u32 running_mode = POWERINTDOS; // 运行模式
u32 Path_Addr;
u8 *font, *ascfont, *hzkfont;
u8 *IVT;

void c_pgui_main(void);
void shell(void) {
  //   ide_initialize(0x1F0, 0x3F6, 0x170, 0x376, 0x000);
  //   init_palette();
  //   vfs_mount_disk(current_task()->drive, current_task()->drive);
  //   vfs_change_disk(current_task()->drive);
  //   env_init();
  //   init_networkCTL();
  //   init_network();

  //   if (env_read("network") == NULL) {
  //     printk("would you like to enable network?(y/n)\n");
  //     switch (getch()) {
  //       case 'y':
  //       case 'Y':
  //         env_write("network", "enable");
  //         break;
  //       default:
  //         env_write("network", "disable");
  //         break;
  //     }
  //   }
  //   if (strcmp(env_read("network"), "enable") == 0) {
  //     init_card();
  //     //for(;;);
  //   }
  //   init_ok_flag = 1;
  //   /*到这里 系统的初始化才真正结束*/
  //   font = (u8*)"/other/font.bin";
  //   FILE* fp = fopen("/other/font.bin", "r");
  //   ascfont = fp->buffer;
  //   fp = fopen("/other/hzk16", "r");
  //   hzkfont = fp->buffer;
  // retry:
  //   if (!(Path_Addr = env_read("path"))) {
  //     env_write("path", "");
  //     goto retry;
  //   }
  //   clear();

  //   printk("Please choose your boot mode:\n");
  //   printk("1. TextMode 80 * 25\n");
  //   printk("2. HighTextMode 128 * 48\n");
  //   printk("Input:");
  //   u8 choice;
  //   while (true) {
  //     choice = getch();
  //     if (choice == '1') {
  //       running_mode = POWERINTDOS;
  //       clear();
  //       break;
  //     } else if (choice == '2') {
  //       running_mode = HIGHTEXTMODE;
  //       SwitchToHighTextMode();
  //       break;
  //     }
  //   }
  //   if (fsz("AUTOEXEC.BAT") == -1) {
  //     printk("Boot Warning:No AUTOEXEC.BAT in Drive %c\n", current_task()->drive);
  //   } else {
  //     run_bat("AUTOEXEC.BAT");
  //   }
  //   extern struct tty* tty_default;
  //   tty_set(current_task(), tty_default);
  //   shell_handler();
}
void shell_handler() {
  // struct TASK* task = current_task();
  // task->line = (char*)page_malloc(1024);
  // char buf[255];
  // while (1) {
  //   vfs_getPath(buf);
  //   printk("%s>", buf);
  //   clean(task->line, 1024);
  //   input(task->line, 1024);
  //   command_run(task->line);
  // }
}
struct tty *now_tty() {
  extern struct List *tty_list;
  struct tty         *n;
  for (int j = 1; FindForCount(j, tty_list) != 0; j++) {
    n = (struct tty *)FindForCount(j, tty_list)->val;
    if ((now_tty_TextMode(n) && running_mode == POWERINTDOS) ||
        (now_tty_HighTextMode(n) && running_mode == HIGHTEXTMODE)) {
      return n;
    }
  }
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
  //       extern struct TASK* last_fpu_task;
  //       if (last_fpu_task == task) {
  //         printk("Set last fpu task %d * 8 %s.\n", task->sel / 8, task->name);
  //         last_fpu_task = (struct TASK *)0x1;
  //       }
  //       __sub_task(task);
  //       goto re;
  //     }
  //   }
  //   task_sleep(current_task());
  // }
}
void com_input(char *ptr, int len) {
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

  // while (true) {
  // }
  // while (true) {
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
  //     u32 *r = data[3];
  //     *r = result;
  //   }
  // }
}
