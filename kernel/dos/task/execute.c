#include <ELF.h>
#include <dos.h>
extern char *shell_data;
extern struct TSS32 tss;
void task_app(char *filename) {
  while (!current_task()->line)
    ;

  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf = (char *)page_malloc_one();
  char *mbuf = (char *)page_malloc_one();
  fifo8_init((struct FIFO8 *)kfifo, 4096, (unsigned char *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (unsigned char *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
  current_task()->alloc_size = 1 * 1024 * 1024;
  char tmp[100];
  task_to_user_mode_elf(filename);
  for (;;)
    ;
}
void task_shell() {
  while (!current_task()->line)
    ;
  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf = (char *)page_malloc_one();
  char *mbuf = (char *)page_malloc_one();
  fifo8_init((struct FIFO8 *)kfifo, 4096, (unsigned char *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (unsigned char *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
  current_task()->alloc_size = 1 * 1024 * 1024;
  char tmp[100];
  task_to_user_mode_shell();
  for (;;)
    ;
}
void task_to_user_mode_shell() {

  unsigned addr = (unsigned)current_task()->top;

  addr -= sizeof(intr_frame_t);
  intr_frame_t *iframe = (intr_frame_t *)(addr);

  iframe->edi = 1;
  iframe->esi = 2;
  iframe->ebp = 3;
  iframe->esp_dummy = 4;
  iframe->ebx = 5;
  iframe->edx = 6;
  iframe->ecx = 7;
  iframe->eax = 8;

  iframe->gs = 0;
  iframe->ds = GET_SEL(3 * 8, SA_RPL3);
  iframe->es = GET_SEL(3 * 8, SA_RPL3);
  iframe->fs = GET_SEL(3 * 8, SA_RPL3);
  iframe->ss = GET_SEL(3 * 8, SA_RPL3);
  iframe->cs = GET_SEL(4 * 8, SA_RPL3);
  iframe->eflags = (0 << 12 | 0b10 | 1 << 9);
  iframe->esp = NULL; // 设置用户态堆栈
  char *p = shell_data;
  if (!elf32Validate(p)) {
    page_free(iframe->esp - 512 * 1024, 512 * 1024);
    while (FindForCount(1, vfs_now->path) != NULL) {
      page_free(FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val, 255);
      DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
      extern mtask *mouse_use_task;
      if (mouse_use_task == current_task()) {
        mouse_sleep(&mdec);
      }
    }
    DeleteList(vfs_now->path);
    free(vfs_now->cache);
    page_free((void *)vfs_now, sizeof(vfs_t));
    task_kill(current_task()->tid);
    for (;;)
      ;
  }
  unsigned alloc_addr = (elf32_get_max_vaddr(p) & 0xfffff000) + 0x1000;
  unsigned pg = div_round_up(current_task()->alloc_size, 0x1000);
  for (int i = 0; i < pg + 128; i++) {
    //logk("%d\n",i);
    page_link(alloc_addr + i * 0x1000);
  }
  unsigned alloced_esp = alloc_addr + 128 * 0x1000;
  alloc_addr += 128 * 0x1000;
  iframe->esp = alloced_esp;
  page_link(0xf0000000);
  *(unsigned char *)(0xf0000000) = 1;
  current_task()->alloc_addr = alloc_addr;
  
  iframe->eip = load_elf(p);
  current_task()->user_mode = 1;
  tss.esp0 = current_task()->top;
  change_page_task_id(current_task()->tid, iframe->esp - 512 * 1024,
                      512 * 1024);
  asm volatile("movl %0, %%esp\n"
               "popa\n"
               "pop %%gs\n"
               "pop %%fs\n"
               "pop %%es\n"
               "pop %%ds\n"
               "iret" ::"m"(iframe));
  for (;;)
    ;
}
void task_to_user_mode_elf(char *filename) {

  unsigned addr = (unsigned)current_task()->top;

  addr -= sizeof(intr_frame_t);
  intr_frame_t *iframe = (intr_frame_t *)(addr);

  iframe->edi = 1;
  iframe->esi = 2;
  iframe->ebp = 3;
  iframe->esp_dummy = 4;
  iframe->ebx = 5;
  iframe->edx = 6;
  iframe->ecx = 7;
  iframe->eax = 8;

  iframe->gs = 0;
  iframe->ds = GET_SEL(3 * 8, SA_RPL3);
  iframe->es = GET_SEL(3 * 8, SA_RPL3);
  iframe->fs = GET_SEL(3 * 8, SA_RPL3);
  iframe->ss = GET_SEL(3 * 8, SA_RPL3);
  iframe->cs = GET_SEL(4 * 8, SA_RPL3);
  iframe->eflags = (0 << 12 | 0b10 | 1 << 9);
  iframe->esp = NULL; // 设置用户态堆栈
  char *p = page_malloc(vfs_filesize(filename));
  vfs_readfile(filename, p);
  if (!elf32Validate(p)) {
    page_free(iframe->esp - 512 * 1024, 512 * 1024);
    page_free(p, vfs_filesize(filename));
    while (FindForCount(1, vfs_now->path) != NULL) {
      page_free(FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val, 255);
      DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
      extern mtask *mouse_use_task;
      if (mouse_use_task == current_task()) {
        mouse_sleep(&mdec);
      }
    }
    DeleteList(vfs_now->path);
    free(vfs_now->cache);
    page_free((void *)vfs_now, sizeof(vfs_t));
    task_kill(current_task()->tid);
    for (;;)
      ;
  }
  unsigned alloc_addr = (elf32_get_max_vaddr(p) & 0xfffff000) + 0x1000;
  unsigned pg = div_round_up(current_task()->alloc_size, 0x1000);
  for (int i = 0; i < pg+128; i++) {
    //logk("link %08x\n",alloc_addr + i * 0x1000);
    page_link(alloc_addr + i * 0x1000);
  }
  unsigned alloced_esp = alloc_addr + 128 * 0x1000;
  alloc_addr += 128 * 0x1000;
  iframe->esp = alloced_esp;
  if (current_task()->ptid != -1) {
    page_link(0xf0000000);
    *(unsigned char *)(0xf0000000) = 0;
  }
  // *(unsigned int *)(0xb5000000) = 2;
  // logk("value = %08x\n",*(unsigned int *)(0xb5000000));
  current_task()->alloc_addr = alloc_addr;
  iframe->eip = load_elf(p);
  current_task()->user_mode = 1;
  tss.esp0 = current_task()->top;
  change_page_task_id(current_task()->tid, p, vfs_filesize(filename));
  change_page_task_id(current_task()->tid, iframe->esp - 512 * 1024,
                      512 * 1024);
  asm volatile("movl %0, %%esp\n"
               "popa\n"
               "pop %%gs\n"
               "pop %%fs\n"
               "pop %%es\n"
               "pop %%ds\n"
               "iret" ::"m"(iframe));
  for (;;)
    ;
}
void os_execute(char *filename, char *line) {
  extern mtask *mouse_use_task;
  mtask *backup = mouse_use_task;
  extern int init_ok_flag;
  unsigned int *stack =
      (unsigned int *)(page_malloc(64 * 1024) + 64 * 1024 - 4);
  char *fm = (char *)page_malloc(strlen(filename) + 1);
  strcpy(fm, filename);
  *stack = (unsigned int)(fm);
  init_ok_flag = 0;
  mtask *t = create_task(task_app, (unsigned)stack - 4, 1, 1);
  vfs_change_disk_for_task(current_task()->nfs->drive, t);
  List *l;
  char *path;
  for (int i = 1; FindForCount(i, current_task()->nfs->path) != NULL; i++) {
    l = FindForCount(i, current_task()->nfs->path);
    path = (char *)l->val;
    t->nfs->cd(t->nfs, path);
  }
  init_ok_flag = 1;
  t->ptid = current_task()->tid;
  struct tty *tty_backup = current_task()->TTY;
  t->TTY = current_task()->TTY;
  current_task()->TTY = NULL;
  char *p1 = page_malloc(strlen(line) + 1);
  strcpy(p1, line);
  int o = current_task()->fifosleep;
  current_task()->fifosleep = 1;
  t->line = p1;
  waittid(t->tid);
  current_task()->fifosleep = o;
  stack = ((unsigned)stack + 4 - 64 * 1024);
  page_free(stack, 64 * 1024);
  page_free(p1, strlen(p1) + 1);
  page_free(fm, strlen(fm) + 1);
  current_task()->TTY = tty_backup;
  if (backup) {
    mouse_ready(&mdec);
    mouse_use_task = backup;
  } else {
    mouse_sleep(&mdec);
  }
}
void os_execute_shell(char *line) {
  extern int init_ok_flag;
  unsigned int *stack = (unsigned int *)(page_malloc(64 * 1024) + 64 * 1024);
  init_ok_flag = 0;
  mtask *t = create_task(task_shell, (unsigned)stack, 1, 1);
  t->nfs = current_task()->nfs;
  init_ok_flag = 1;
  t->ptid = current_task()->tid;
  struct tty *tty_backup = current_task()->TTY;
  t->TTY = current_task()->TTY;
  current_task()->TTY = NULL;
  char *p1 = page_malloc(strlen(line) + 1);
  strcpy(p1, line);
  int o = current_task()->fifosleep;
  current_task()->fifosleep = 1;
  t->line = p1;
 // io_sti();
  waittid(t->tid);
  current_task()->fifosleep = o;
  stack = ((unsigned)stack - 64 * 1024);
  page_free(stack, 64 * 1024);
  page_free(p1, strlen(p1) + 1);
  current_task()->TTY = tty_backup;
}
void os_execute_no_ret(char *filename, char *line) {
  unsigned int *stack =
      (unsigned int *)(page_malloc(64 * 1024) + 64 * 1024 - 4);
  *stack = (unsigned int)(filename);
  mtask *t = create_task(task_app, (unsigned)stack - 4, 1, 1);
  struct tty *tty_backup = current_task()->TTY;
  t->TTY = current_task()->TTY;
  current_task()->TTY = NULL;
  t->line = line;
}