// 杂项函数
// Copyright (C) 2021-2022 zhouzhihao & min0911_
// ------------------------------------------------
#include <dos.h>

int      DisableExpFlag = 0;
uint32_t CatchEIP       = 0;
char     flagOfexp      = 0;
char     public_catch   = 0;
int      st_task        = 0;
// 得到 cr0 寄存器
uint32_t get_cr0() {
  // 直接将 mov eax, cr0，返回值在 eax 中
  asm volatile("movl %cr0, %eax\n");
}

// 设置 cr0 寄存器，参数是页目录的地址
void set_cr0(uint32_t cr0) {
  asm volatile("movl %%eax, %%cr0\n" ::"a"(cr0));
}
void SwitchPublic() {
  public_catch = 1;
}
void SwitchPrivate() {
  public_catch = 0;
}
void disableExp() {
  if (public_catch) {
    DisableExpFlag = 1;
  } else {
    current_task()->DisableExpFlag = 1;
  }
}
void EnableExp() {
  if (public_catch) {
    DisableExpFlag = 0;
  } else {
    current_task()->DisableExpFlag = 0;
  }
}
char GetExpFlag() {
  // printk("Get.\n");
  if (public_catch) {
    return flagOfexp;
  } else {
    return current_task()->flagOfexp;
  }
}
void ClearExpFlag() {
  if (public_catch) {
    flagOfexp = 0;
  } else {
    current_task()->flagOfexp = 0;
  }
}
void SetCatchEip(uint32_t eip) {
  // printk("eip = %08x\n",eip);
  if (public_catch) {
    CatchEIP = eip;
  } else {
    current_task()->CatchEIP = eip;
  }
}
void print_32bits_ascil(u32 n);
// 保护模式调用BIOS中断的驱动
void INT(u8 intnum, regs16_t *regs) {
  extern u8                 *IVT;
  struct SEGMENT_DESCRIPTOR *gdt = (struct SEGMENT_DESCRIPTOR *)ADR_GDT;
  // logk("%08x\n",current_task()->pde);
  set_segmdesc(gdt + 1000, 0xffffffff, 0, AR_CODE32_ER); // CODE32
  set_segmdesc(gdt + 1001, 0xfffff, 0, AR_CODE16_ER);    // CODE16
  set_segmdesc(gdt + 1002, 0xfffff, 0, AR_DATA16_RW);    // DATA16
  memcpy(0, IVT, 0x400);
  int32(intnum, regs);
  set_segmdesc(gdt + 1000, 0, 0, 0); // 临时GDT清零
  set_segmdesc(gdt + 1001, 0, 0, 0);
  set_segmdesc(gdt + 1002, 0, 0, 0);

  set_cr3(current_task()->pde);
  io_sti();
}
void insert_char(char *str, int pos, char ch) {
  int i;
  for (i = strlen(str); i >= pos; i--) {
    str[i + 1] = str[i];
  }
  str[pos] = ch;
}
void delete_char(char *str, int pos) {
  int i;
  for (i = pos; i < strlen(str); i++) {
    str[i] = str[i + 1];
  }
}
mtask *last_fpu_task = NULL;
void   fpu_disable() {
  set_cr0(get_cr0() | (CR0_EM | CR0_TS));
}
void fpu_enable(mtask *task) {
  set_cr0(get_cr0() & ~(CR0_EM | CR0_TS));
  if (!task->fpu_flag) {
    asm volatile("fnclex \n"
                 "fninit \n");
    memset(&task->fpu, 0, sizeof(fpu_t));
    logk("FPU create state for task 0x%08x\n", task);
  } else {
    asm volatile("frstor (%%eax) \n" ::"a"(&(task->fpu)));
  }
  task->fpu_flag = 1;
}

#define _ERROR(_code_, _tips_)                                                                     \
  void ERROR##_code_(uint32_t eip) {                                                               \
    uint32_t *esp = &eip;                                                                          \
    ERROR(_code_, _tips_);                                                                         \
    if (public_catch) {                                                                            \
      *esp = CatchEIP;                                                                             \
    } else {                                                                                       \
      *esp = current_task()->CatchEIP;                                                             \
    }                                                                                              \
  }

_ERROR(0, "#DE");
_ERROR(1, "#DB");
_ERROR(3, "#BP");
_ERROR(4, "#OF");
_ERROR(5, "#BR");
_ERROR(6, "#UD");
_ERROR(8, "#DF");
_ERROR(9, "#MF");
_ERROR(10, "#TS");
_ERROR(11, "#NP");
_ERROR(12, "#SS");
_ERROR(16, "#MF");
_ERROR(17, "#AC");
_ERROR(18, "#MC");
_ERROR(19, "#XF");

#undef _ERROR

int  dflag = 0;
bool has_fpu_error() {
  dflag = 0;
  uint16_t status_word;
  asm("fnstsw %0" : "=m"(status_word));
  dflag = 1;
  return (status_word & 0x1F) != 0;
}
void ERROR7(uint32_t eip) {
  if (current_task()->fpu_flag > 1 || current_task()->fpu_flag < 0) {
    set_cr0(get_cr0() & ~(CR0_EM | CR0_TS));
    return;
  }
  fpu_enable(current_task());
}

void ERROR13(uint32_t eip) {
  printk("ERROR GP!!!!");
  for (;;)
    ;
}

void ERROR(int CODE, char *TIPS) {
  if (public_catch) {
    flagOfexp = 1;
  } else {
    current_task()->flagOfexp = 1;
  }
  logk("DisableExpFlag = %d\n", public_catch ? DisableExpFlag : current_task()->DisableExpFlag);
  if (public_catch) {
    if (DisableExpFlag) { return; }
  } else {
    if (current_task()->DisableExpFlag) { return; }
  }
  io_cli();
  irq_mask_set(0);
  logk("%s\n", TIPS);
  logk("Error Code: %d\n", CODE);
  SwitchToText8025_BIOS();
  extern struct tty *tty_default;
  tty_set(current_task(), tty_default);
  clear();
  int i, j;
  for (i = 0; i < 160; i++) {
    for (j = 0; j < 25; j++) {
      // 将屏幕背景色改为蓝底白字
      if (i % 2 == 1) { *(char *)(0xb8000 + j * 160 + i) = 0x1f; }
    }
  }
  current_task()->TTY->color = 0x1f;
  gotoxy(0, 0);
  // beep(2, 7, 8);
  printk("Sorry!Your computer has some problem!\n");
  printk("Please check your computer and try again.Or Restart your computer.\n");
  printk("Now You just look at the Error Message.\n");
  printk("You can rebuild PowerintDOS 386 too.\n");
  printk("The System Version:%s\n", VERSION);
  printk("Error Code:%08x\n", CODE);
  printk("Error Message:%s\n", TIPS);

  // printk("Task sel=%d\n", current_task()->sel);
  io_cli();
  for (;;) {}
}
void KILLAPP(int eip, int ec) {
  printk("KILL APP %08x %d\n", eip, ec);
  // while (FindForCount(1, vfs_now->path) != NULL) {
  //   // printk("%d\n",vfs_now->path->ctl->all);
  //   page_free(FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val,
  //   255); DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
  // }
  // DeleteList(vfs_now->path);
  // page_free((void*)vfs_now, sizeof(vfs_t));
  // mtask* task = current_task();
  // if (task->is_child) {
  //   task = task->thread.father;  // 找你家长，乱搞！
  // }
  // if (ec == 0xff) {  // 返回系统快捷键
  //   printk("\nSystem Protect:Break Key(F1).\n");
  // } else {
  //   printk("\nSystem Protect:The program name:%s TASK ID:%d EC:%x
  //   EIP:%08x\n",
  //          task->name, task->sel / 8 - 103, ec, eip);
  // }
  //  task_delete(task);
  for (;;)
    ;
}
void KILLAPP0(int ec, int tn) {
  // while (FindForCount(1, vfs_now->path) != NULL) {
  //   // printk("%d\n",vfs_now->path->ctl->all);
  //   page_free(FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val,
  //   255); DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
  // }
  // DeleteList(vfs_now->path);
  // page_free((void*)vfs_now, sizeof(vfs_t));
  // mtask* task = get_task(tn);
  // if (task->is_child) {
  //   task = task->thread.father;  // 找你家长，乱搞！
  // }
  // if (ec == 0xff) {  // 返回系统快捷键
  //   printk("\nSystem Protect:Break Key(F1).\n");
  // }
  // io_sti();
  // task_delete(task);
  return;
}
char bcd2hex(char bcd) {
  char i;
  if (bcd > 0x10 || bcd == 0x10) {
    i = (bcd / 0x10) * 6;
    i = i + bcd - (bcd / 0x10) * 0x10;
    return i;
  } else {
    return bcd;
  }
}
char hex2bcd(char hex) {
  char i;
  if (hex > 0x0a || hex == 0x0a) {
    i = (hex / 0x0a) * 0x10;
    i = i + hex - (hex / 0x0a) * 0x0a;
    return i;
  } else {
    return hex;
  }
}
void Print_Hex(unsigned x) {
  printk("%08x", x);
  // for(int i = 0;i<0x03000000;i++);
}
void Clear_A_Line() {
  // printk("\n");
}
void getCPUBrand(char *cBrand) {
  print_32bits_ascil(get_cpu4(0x80000002));
  print_32bits_ascil(get_cpu5(0x80000002));
  print_32bits_ascil(get_cpu6(0x80000002));
  print_32bits_ascil(get_cpu7(0x80000002));
  print_32bits_ascil(get_cpu4(0x80000003));
  print_32bits_ascil(get_cpu5(0x80000003));
  print_32bits_ascil(get_cpu6(0x80000003));
  print_32bits_ascil(get_cpu7(0x80000003));
  print_32bits_ascil(get_cpu4(0x80000004));
  print_32bits_ascil(get_cpu5(0x80000004));
  print_32bits_ascil(get_cpu6(0x80000004));
  print_32bits_ascil(get_cpu7(0x80000004));
  printk("\n");
}
char ascii2num(char c) {
  if (c > 'A' - 1 && c < 'F' + 1) {
    c = c - 0x37;
  } else if (c > 'a' - 1 && c < 'f' + 1) {
    c = c - 0x57;
  } else if (c > '0' - 1 && c < '9' + 1) {
    c = c - 0x30;
  }
  return c;
}
char num2ascii(char c) {
  if (c > 0x9 && c < 0x10) {
    c = c + 0x37;
  } else if (c < 0x0a) {
    c = c + 0x30;
  }
  return c;
}
void strtoupper(char *str) {
  while (*str != '\0') {
    if (*str >= 'a' && *str <= 'z') { *str -= 32; }
    str++;
  }
}
int GetCHorEN(u8 *str) {
  // 获取这个字符是中文全角还是英文半角
  if (str[0] > 0x80 && str[1] > 0x80) {
    return 1;
  } else if (str[0] > 0x80 && str[1] < 0x80) {
    return 0;
  } else {
    return 0;
  }
}
void clean(char *s, int len) {
  // 清理某个内存区域（全部置0）
  int i;
  for (i = 0; i != len; i++) {
    s[i] = 0;
  }
  return;
}
void print_32bits_ascil(u32 n) {
  char str[32];
  int  i = 0;
  for (i = 0; i < 4; i++) {
    str[i]   = n & 0xff;
    n      >>= 8;
  }

  for (i = 0; i < 4; i++) {
    printchar(str[i]);
  }
}