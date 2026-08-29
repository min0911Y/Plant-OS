#include <arch/x86/io.h>
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
    if ((x86_port_read8(PORT_KEYSTA) & KEYSTA_SEND_NOTREADY) == 0) {
      break;
    }
  }
  return;
}

void init_keyboard(void) {
  /* 初始化键盘控制电路 */
  wait_KBC_sendready();
  x86_port_write8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
  wait_KBC_sendready();
  x86_port_write8(PORT_KEYDAT, KBC_MODE);
  return;
}
int getch() {
  unsigned char ch;
  ch = input_char_inSM(); // 扫描码
  if (ch == 0xe0) {       // keytable之外的键（↑,↓,←,→）
    ch = input_char_inSM();
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
  return 0;
}
extern struct tty *tty_default;
static struct tty *tty_for_input(void) {
  mtask *task = current_task();
  if (task != NULL && task->TTY != NULL && task->TTY->using1 == 1) {
    return task->TTY;
  }
  return tty_default;
}
int tty_fifo_status() {
  struct tty *tty = tty_for_input();
  if (tty == NULL) {
    return 0;
  }
  return tty->fifo_status(tty);
}
int tty_fifo_get() {
  struct tty *tty = tty_for_input();
  if (tty == NULL) {
    return -1;
  }
  return tty->fifo_get(tty);
}
int input_char_inSM() {
  for (;;) {
    if (tty_fifo_status() != 0) {
      int input = tty_fifo_get();
      if (input != -1) {
        return input;
      }
    }
    task_fall_blocked_reason(WAITING, WAIT_REASON_KEYBOARD);
  }
}
int kbhit() {
  return tty_fifo_status() != 0; // 进程的键盘FIFO缓冲区是否为空
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
  return 0;
}
int disable_flag = 0;
mtask *keyboard_use_task = NULL;

static void keyboard_wake_task(mtask *task) {
  if (task == NULL) {
    return;
  }
  task->weight = 5;
  task_run(task);
  if (task == current_task()) {
    return;
  }
  mtask_run_now(task);
  task_next();
}

static mtask *keyboard_foreground_task(void) {
  struct tty *foreground = now_tty();
  if (foreground == NULL) {
    return NULL;
  }

  mtask *process = NULL;
  mtask *fallback = NULL;
  for (unsigned tid = 0; tid < 255; tid++) {
    mtask *task = get_task(tid);
    if (task == NULL || task->TTY != foreground || task->keyfifo == NULL ||
        task->state == DIED || task->terminate_pending) {
      continue;
    }
    if (task == current_task()) {
      return task;
    }
    if (process == NULL && task->kind == TASK_PROCESS) {
      process = task;
    }
    if (fallback == NULL) {
      fallback = task;
    }
  }
  return process != NULL ? process : fallback;
}

void inthandler21(int *esp) {
  // 键盘中断处理函数
  unsigned char data;
  (void)esp;
  send_eoi(1);
  data = x86_port_read8(PORT_KEYDAT); // 从键盘IO口读取扫描码
  //  特殊键处理
  if (data == 0xe0) {
    e0_flag = 1;
    return;
  }
  if (data == 0x2a || data == 0x36) { // Shift按下
    shift = 1;
  }
  if (data == 0x1d) { // Shift按下
    ctrl = 1;
  }
  if (data == 0x3a) { // Caps Lock按下
    caps_lock = caps_lock ^ 1;
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
      if (get_task(i)->sigint_up) {
        get_task(i)->signal |= SIGMASK(SIGINT);
      }
    }
    // return;
  }
  // } else if (data >= 0x3b && data <= 0x47 && shift) {
  //   // 按下F1 ~ F12 & Shift
  //   // if (running_mode == POWERINTDOS) {
  //   //   SwitchShell_TextMode(data - 0x3b + 1);  // 换到1~12号控制台
  //   //   return;
  //   // } else if (running_mode == HIGHTEXTMODE) {
  //   //   SwitchShell_HighTextMode(data - 0x3b + 1);
  //   //   return;
  //   // }
  // }
  // 普通键处理
  if (data >= 0x80) {
    // printk("press\n");
    if (disable_flag && keyboard_use_task) {
      if (keyboard_use_task->keyboard_release != NULL) {
        // TASK结构体中有对按下键特殊处理的
        if (e0_flag) {
          keyboard_use_task->keyboard_release(0xe0, keyboard_use_task->tid);
        }
        keyboard_use_task->keyboard_release(
            data, keyboard_use_task->tid); // 处理按下键
      }
      keyboard_wake_task(keyboard_use_task);
    } else
      for (int i = 0; i < 255; i++) {
        if (!get_task(i)) {
          continue;
        }
        if (get_task(i)->keyboard_release != NULL) {
          // TASK结构体中有对松开键特殊处理的
          if (e0_flag) {
            get_task(i)->keyboard_release(0xe0, i);
          }
          get_task(i)->keyboard_release(data, i); // 处理松开键

          if (disable_flag) {
          }
        }
      }
    if (e0_flag == 1)
      e0_flag = 0;
    return;
  }
  if (disable_flag && keyboard_use_task) {
    if (keyboard_use_task->keyboard_press != NULL) {
      // TASK结构体中有对按下键特殊处理的
      if (e0_flag) {
        keyboard_use_task->keyboard_press(0xe0, keyboard_use_task->tid);
      }
      keyboard_use_task->keyboard_press(data,
                                        keyboard_use_task->tid); // 处理按下键
    }
    keyboard_wake_task(keyboard_use_task);
  } else
    for (int i = 0; i < 255; i++) {
      // printk("up\n");
      if (!get_task(i)) {
        continue;
      }
      if (get_task(i)->keyboard_press != NULL) {
        // TASK结构体中有对按下键特殊处理的
        if (e0_flag) {
          get_task(i)->keyboard_press(0xe0, i);
        }
        get_task(i)->keyboard_press(data, i); // 处理按下键
      }
    }
  if (disable_flag == 0) {
    mtask *task = keyboard_foreground_task();
    if (task != NULL) {
      if (e0_flag) {
        fifo8_put(task_get_key_fifo(task), 0xe0);
      }
      fifo8_put(task_get_key_fifo(task), data);
      keyboard_wake_task(task);
    }
  }
  if (e0_flag == 1)
    e0_flag = 0;
  return;
}
