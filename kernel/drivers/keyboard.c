#include <dos.h>
#include <drivers.h>
#define KEYSTA_SEND_NOTREADY 0x02
#define KEYCMD_WRITE_MODE 0x60
#define KBC_MODE 0x47
static int caps_lock, shift, e0_flag = 0, ctrl = 0;
char keytable[0x54] = { // 按下Shift
    0,    0x01, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',
    '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',  '}',
    10,   0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',
    0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',  'D', '8', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
char keytable1[0x54] = { // 未按下Shift
    0,    0x01, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    10,   0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',  '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
void wait_KBC_sendready(void) {
  /* 等待键盘控制电路准备完毕 */
  for (;;) {
    if ((io_in8(PORT_KEYSTA) & KEYSTA_SEND_NOTREADY) == 0) {
      break;
    }
  }
  return;
}

void init_keyboard(void) {
  /* 初始化键盘控制电路 */
  wait_KBC_sendready();
  io_out8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
  wait_KBC_sendready();
  io_out8(PORT_KEYDAT, KBC_MODE);
  return;
}
int getch() {
  unsigned char ch;
  ch = input_char_inSM(); // 扫描码
  if (ch > 0x80) {        // keytable之外的键（↑,↓,←,→）
    ch -= 0x80;
    if (ch == 0x48) { // ↑
      return -1;
    } else if (ch == 0x50) { // ↓
      return -2;
    } else if (ch == 0x4b) { // ←
      return -3;
    } else if (ch == 0x4d) { // →
      return -4;
    }
  }
  // 返回扫描码（keytable之内）对应的ASCII码
  if (keytable[ch] == 0x00) {
    return 0;
  }
  if (shift == 0 && caps_lock == 0) {
    return keytable1[ch];
  } else if (shift == 1 || caps_lock == 1) {
    return keytable[ch];
  } else if (shift == 1 && caps_lock == 1) {
    return keytable1[ch];
  }
}
int input_char_inSM() {
  int i;
  mtask *task = current_task();
  while (1) {
    if ((fifo8_status(task_get_key_fifo(task)) == 0)) {
      // 不返回扫描码的情况
      // 1.没有输入
      // 2.窗口未处于顶端
      // 3.正在运行的控制台并不是函数发起的控制台（TTY）
    } else {
      // 返回扫描码
      i = fifo8_get(
          task_get_key_fifo(current_task())); // 从FIFO缓冲区中取出扫描码
      if (i != -1) {
        break;
      }
    }
  }
  return i;
}
int kbhit() {
  return fifo8_status(current_task()->keyfifo) !=
         0; // 进程的键盘FIFO缓冲区是否为空
}
int sc2a(int sc) {
  // 扫描码转化ASCII码
  // 逻辑与getch函数大同小异
  int ch = sc;
  if (ch > 0x80) {
    ch -= 0x80;
    if (ch == 0x48) {
      return -1;
    } else if (ch == 0x50) {
      return -2;
    } else if (ch == 0x4b) {
      return -3;
    } else if (ch == 0x4d) {
      return -4;
    }
  }
  if (keytable[ch] == 0x00) {
    return 0;
  }
  if (shift == 0 && caps_lock == 0) {
    return keytable1[ch];
  } else if (shift == 1 || caps_lock == 1) {
    return keytable[ch];
  } else if (shift == 1 && caps_lock == 1) {
    return keytable1[ch];
  }
}

void inthandler21(int *esp) {
  // 键盘中断处理函数
  unsigned char data, s[4];
  io_out8(PIC0_OCW2, 0x61);
  data = io_in8(PORT_KEYDAT); // 从键盘IO口读取扫描码
  //  特殊键处理
  if (data == 0xe0) {
    e0_flag = 0x80;
    return;
  }
  if (e0_flag) {
    if (data > 0x80) {
      e0_flag = 0;
    }
  }
  if (data == 0x2a || data == 0x36) { // Shift按下
    shift = 1;
  }
  if (data == 0x1d) { // Shift按下
    ctrl = 1;
  }
  if (data == 0x3a) { // Caps Lock按下
    caps_lock = caps_lock ^ 1;
    return;
  }
  if (data == 0xaa || data == 0xb6) { // Shift松开
    shift = 0;
  }
  if (data == 0x9d) { // Shift按下
    ctrl = 0;
  }
  // 快捷键处理
  if (data == 0x2e && ctrl) {
    for (int i = 0; i < 255; i++) {
      if (!get_task(i)) {
        continue;
      }
      if(get_task(i)->sigint_up) {
        get_task(i)->signal |= SIGMASK(SIGINT);
      }
    }
    return;
  }
  // } else if (data >= 0x3b && data <= 0x47 && shift) {
  //   // 按下F1 ~ F12 & Shift
  //   // if (running_mode == POWERINTDOS) {
  //   //   SwitchShell_TextMode(data - 0x3b + 1);  // 换到1~12号控制台
  //   //   io_sti();
  //   //   return;
  //   // } else if (running_mode == HIGHTEXTMODE) {
  //   //   SwitchShell_HighTextMode(data - 0x3b + 1);
  //   //   io_sti();
  //   //   return;
  //   // }
  // }
  // 普通键处理
  if (data >= 0x80) {
    // printk("press\n");
    for (int i = 0; i < 255; i++) {
      if (!get_task(i)) {
        continue;
      }
      if (get_task(i)->keyboard_release != NULL) {
        // TASK结构体中有对松开键特殊处理的
        get_task(i)->keyboard_release(data, i); // 处理松开键
      }
    }
    return;
  }
  for (int i = 0; i < 255; i++) {
    // printk("up\n");
    if (!get_task(i)) {
      continue;
    }
    if (get_task(i)->keyboard_press != NULL) {
      // TASK结构体中有对按下键特殊处理的
      get_task(i)->keyboard_press(data, i); // 处理按下键
    }
  }
  for (int i = 0; i < 255; i++) {
    if (!get_task(i)) {
      continue;
    }
    // 按下键通常处理
    mtask *task = get_task(i); // 每个进程都处理一遍
    if (task->state != RUNNING || task->fifosleep) {
      if (task->state == WAITING && task->waittid == -1) {
        goto THROUGH;
      }
      // 如果进程正在休眠或被锁了
      continue;
    }
    // 一般进程
  THROUGH:
    //    logk("send\n");
    fifo8_put(task_get_key_fifo(task), data + e0_flag);
  }
  return;
}
