#include <dos.h>
#define IDX(addr)  ((unsigned)addr >> 12)           // 获取 addr 的页索引
#define DIDX(addr) (((unsigned)addr >> 22) & 0x3ff) // 获取 addr 的页目录索引
#define TIDX(addr) (((unsigned)addr >> 12) & 0x3ff) // 获取 addr 的页表索引
#define PAGE(idx)  ((unsigned)idx << 12) // 获取页索引 idx 对应的页开始的位置
unsigned                 div_round_up(unsigned num, unsigned size);
extern struct PAGE_INFO *pages;
unsigned                 custom_handler     = 0;
unsigned                 custom_handler_pde = 0;
void   page_set_physics_attr_pde(u32 vaddr, void *paddr, u32 attr, unsigned pde_backup);
mtask *custom_handler_owner;
void   kbd_press(u8 dat, u32 task) {
  fifo8_put(get_task(task)->Pkeyfifo, dat);
}
void kbd_up(u8 dat, u32 task) {
  fifo8_put(get_task(task)->Ukeyfifo, dat);
}
void user_thread_into() {
  while (!current_task()->line)
    ; // 等待配置
  unsigned *r = current_task()->line;
  unsigned  eip;
  unsigned  esp;
  eip = r[1];
  esp = r[0];
  page_free_one(r);
  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf  = (char *)page_malloc_one();
  char *mbuf  = (char *)page_malloc_one();
  fifo8_init((struct FIFO8 *)kfifo, 4096, (u8 *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (u8 *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
  char tmp[100];
  task_to_user_mode(eip, esp);
  while (true) {}
}

void test(u32 b) {
  return;
}
void *mem_alloc_nb(memory *mem, u32 size, u32 n) {
  size = (size + 0xfff) & 0xfffff000;
  return mem_alloc(mem, size);
}

void *malloc_app_heap(void *alloc_addr, u32 ds_base, u32 size) {
  void *p   = mem_alloc_nb(alloc_addr, size + sizeof(int), 0x1000);
  *(int *)p = size;
  return (char *)p + sizeof(int);
}
void free_app_heap(void *alloc_addr, u32 ds_base, void *p) {}
enum {
  EDI,
  ESI,
  EBP,
  ESP,
  EBX,
  EDX,
  ECX,
  EAX,
  M_PDE,
  C_PDE
};
void inthandler36(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax) {
  // PowerintDOS API
  io_sti();
  mtask  *task       = current_task();
  int     ds_base    = 0;
  void   *alloc_addr = task->alloc_addr;
  int     alloc_size = *(task->alloc_size);
  memory *current_mm = task->mm;
  int    *reg        = &eax + 1; /* eax后面的地址*/
                                 /*强行改写通过PUSHAD保存的值*/
  // reg[0] : EDI
  // reg[1] : ESI
  // reg[2] : EBP
  // reg[3] : ESP
  // reg[4] : EBX
  // reg[5] : EDX
  // reg[6] : ECX
  // reg[7] : EAX
  if (eax == 0x01) {
    reg[EDX] = 0x302e3762;
  } else if (eax == 0x02) {
    printchar((edx & 0x000000ff));
  } else if (eax == 0x03) {
    if (running_mode == POWERINTDOS) {
      if (ebx == 0x01) {
        SwitchToText8025();
      } else if (ebx == 0x02) {
        SwitchTo320X200X256();
      } else if (ebx == 0x03) {
        Draw_Char(ecx, edx, esi, edi);
      } else if (ebx == 0x04) {
        PrintChineseChar(ecx, edx, edi, esi);
      } else if (ebx == 0x05) {
        Draw_Box(ecx, edx, esi, edi, ebp);
      } else if (ebx == 0x06) {
        Draw_Px(ecx, edx, esi);
      } else if (ebx == 0x07) {
        Draw_Str(ecx, edx, (char *)esi + ds_base, edi);
      } else if (ebx == 0x08) {
        PrintChineseStr(ecx, edx, edi, (u8 *)esi + ds_base);
      }
    }
  } else if (eax == 0x04) {
    gotoxy(edx, ecx);
  } else if (eax == 0x05) {
    print((char *)edx + ds_base);
  } else if (eax == 0x06) {
    sleep(edx);
  } else if (eax == 0x08) {
    reg[EDX] = ((int)alloc_addr - ds_base);
  } else if (eax == 0x09) {
    reg[EDX] = alloc_size;
  } else if (eax == 0x0c) {
    Text_Draw_Box(ecx, ebx, esi, edx, (u8)edi);
  } else if (eax == 0x0e) {
    reg[ECX] = get_y();
    reg[EDX] = get_x();
  } else if (eax == 0x0d) {
    beep(ebx, ecx, edx);
  } else if (eax == 0x0f) {
    if (running_mode == POWERINTDOS) {
      mtask *task = current_task();
      int    i, mx1 = task->mx, my1 = task->my, bufx = task->mx * 8, bufy = task->my * 16;
      int    bx  = mx1;
      int    by  = my1;
      int    bmp = *(char *)(task->TTY->vram + by * task->TTY->xsize * 2 + bx * 2 + 1);
      if (mdec.sleep == 1) mouse_ready(&mdec);
      while (true) {
        if (fifo8_status(task_get_mouse_fifo(task)) == 0) {
          task_next();
          signal_deal();
        } else {
          i = fifo8_get(task_get_mouse_fifo(task));
          if (mouse_decode(&mdec, i) != 0) {
            if (task->TTY != now_tty() && task->TTY->is_using == 1) { continue; }
            if (mdec.roll != MOUSE_ROLL_NONE) {
              reg[ECX] = task->mx;
              reg[EDX] = task->my;
              reg[ESI] = 3 + mdec.roll;
              //   mouse_sleep(&mdec);
              *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 + task->mx * 2 + 1) = bmp;
              task->mx                                                                        = mx1;
              task->my                                                                        = my1;
              return;
            }
            mx1   = task->mx;
            my1   = task->my;
            bufx += mdec.x;
            bufy += mdec.y;

            if (bufx > (task->TTY->xsize - 1) * 8) {
              bufx = (task->TTY->xsize - 1) * 8;
            } else if (bufx < 0) {
              bufx = 0;
            }
            if (bufy > (task->TTY->ysize - 1) * 16) {
              bufy = (task->TTY->ysize - 1) * 16;
            } else if (bufy < 0) {
              bufy = 0;
            }
            task->mx                                                              = bufx / 8;
            task->my                                                              = bufy / 16;
            *(char *)(task->TTY->vram + my1 * task->TTY->xsize * 2 + mx1 * 2 + 1) = bmp;
            bmp = *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 + task->mx * 2 + 1);
            *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 + task->mx * 2 + 1) = ~bmp;
            if ((mdec.btn & 0x01) != 0) {
              reg[ECX] = task->mx;
              reg[EDX] = task->my;
              reg[ESI] = 1;
              break;
            } else if ((mdec.btn & 0x02) != 0) {
              reg[ECX] = task->mx;
              reg[EDX] = task->my;
              reg[ESI] = 2;
              break;
            } else if ((mdec.btn & 0x04) != 0) {
              reg[ECX]       = task->mx;
              int alloc_size = *(task->alloc_size);
              reg[EDX]       = task->my;
              reg[ESI]       = 3;
              break;
            }
          }
        }
      }
      *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 + task->mx * 2 + 1) = bmp;
      task->mx                                                                        = mx1;
      task->my                                                                        = my1;
    }
  } else if (eax == 0x10) { // mouse_support
    extern mtask *mouse_use_task;
    logk("%d\n", running_mode == POWERINTDOS && mouse_use_task == NULL);
    reg[EAX] = running_mode == POWERINTDOS && mouse_use_task == NULL;
  } else if (eax == 0x16) {
    if (ebx == 0x01) {
      reg[EDX] = getch();
    } else if (ebx == 0x02) {
      reg[EDX] = input_char_inSM();
    } else if (ebx == 0x03) {
      input((char *)(edx + ds_base), ecx);
    }
  } else if (eax == 0x19) {
    logk("--------------------------------\n");
    logk("c: %08x %s\n", task->pde, (char *)(edx + ds_base));
    reg[EAX] = command_run((char *)(edx + ds_base));
    // asm("xchg %bx,%bx");
    logk("n: %08x\n", task->pde);
    logk("--------------------------------\n");
  } else if (eax == 0x1a) {
    if (ebx == 0x01) {
      int fsize = vfs_filesize((char *)(edx + ds_base));
      reg[EDX]  = fsize;
    } else if (ebx == 0x02) {
      int fsize = vfs_filesize((char *)(ds_base + edx));
      if (fsize != -1) {
        char *q = (char *)ds_base + esi;
        vfs_readfile((char *)(ds_base + edx), q);
        reg[EAX] = 1;
      } else {
        reg[EAX] = 0;
      }
    } else if (ebx == 0x03) {
      char *FilePath = (char *)(ds_base + edx);
      reg[EAX]       = vfs_createfile(FilePath);
    } else if (ebx == 0x04) {
      char *FilePath = (char *)(ds_base + edx);
      reg[EAX]       = vfs_createdict(FilePath);
    } else if (ebx == 0x05) {
      char *FilePath = (char *)(ds_base + edx);
      char *Ptr      = (char *)ds_base + esi;
      int   length   = ecx;
      int   offset   = edi;
      logk("EDIT %s\n", FilePath);
      EDIT_FILE(FilePath, Ptr, length, offset);
    } else if (ebx == 0x06) {
      char        *Path = (char *)(ds_base + edx);
      //  logk("listfile %s\n",Path);
      struct List *file_list = vfs_listfile(Path);
      int          number;
      for (number = 1; FindForCount(number, file_list) != NULL; number++)
        ;
      char *p = ecx;
      for (int i = 1; FindForCount(i, file_list) != NULL; i++) {
        if (p) {
          memcpy((void *)(p + sizeof(vfs_file) * (i - 1)), (void *)FindForCount(i, file_list)->val,
                 sizeof(vfs_file));
        }
        free((void *)FindForCount(i, file_list)->val);
      }
      if (p) memset((void *)(p + (number - 1) * sizeof(vfs_file)), 0, sizeof(vfs_file));
    e:
      DeleteList(file_list);
      reg[EAX] = (int)p - ds_base;
    }
  } else if (eax == 0x1b) {
    int   i;
    char *bes = (char *)(edx + ds_base);
    while (!task->line)
      ;
    for (i = 0; i < strlen(task->line); i++) {
      bes[i] = task->line[i];
    }
    bes[i] = 0;
  } else if (eax == 0x1c) {
    reg[EAX] = Copy((char *)(edx + ds_base), (char *)(esi + ds_base));
  } else if (eax == 0x1d) {
    reg[EAX] = kbhit();
  } else if (eax == 0x1e) {
    // intr_frame_t *i = current_task()->top - sizeof(intr_frame_t);
    // intr_frame1_t *frame = (unsigned *)(i->esp - sizeof(intr_frame1_t));
    // frame->edi = i->edi;
    // frame->esi = i->esi;
    // frame->ebp = i->ebp;
    // frame->esp_dummy = i->esp_dummy;
    // frame->ebx = i->ebx;
    // frame->ecx = i->ecx;
    // frame->edx = i->edx;
    // frame->eax = i->eax;
    // frame->gs = i->gs;
    // frame->fs = i->fs;
    // frame->es = i->es;
    // frame->ds = i->ds;
    // logk("%08x\n", i->eip);
    // frame->eip = i->eip;
    // frame->eip1 = return_to_app;
    // i->eip = test;
    // i->esp = frame;
    // return;
    // while (true)
    //   ;
    if (!(*(u8 *)(0xf0000000))) {
      logk("here\n");
      while (FindForCount(1, vfs_now->path) != NULL) {
        free(FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val);
        DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
        extern mtask *mouse_use_task;
        if (mouse_use_task == task) { mouse_sleep(&mdec); }
      }
      DeleteList(vfs_now->path);
      free(vfs_now->cache);
      free((void *)vfs_now);
    } else {
      get_task(task->ptid)->nfs = task->nfs;
    }
    //  for(;;);
    asm volatile("nop");
    task_exit(ebx);
    while (true)
      ;
  } else if (eax == 0x20) {
    // VBE驱动API
    if (running_mode == POWERINTDOS) {
      if (ebx == 0x01) {
        reg[EAX] = SwitchVBEMode(ecx);
      } else if (ebx == 0x02) {
        reg[EAX] = check_vbe_mode(ecx, (struct VBEINFO *)VBEINFO_ADDRESS);
      } else if (ebx == 0x05) {
        unsigned v = set_mode(ecx, edx, 32);
        logk("reg[EAX] = %08x %d\n", v, pages[IDX(0xfd07c2d0)].count);
        reg[EAX]       = v;
        unsigned count = div_round_up(ecx * edx * 4, 0x1000);
        for (int i = 0; i < count; i++) {
          logk("%08x\n", v + i * 0x1000);
          page_set_physics_attr(v + i * 0x1000, v + i * 0x1000,
                                7); // PG_P | PG_USU | PG_RWW
        }
      }
    }
  } else if (eax == 0x21) {
    if (running_mode == POWERINTDOS) {
      if (ebx == 0x01) {
        SwitchToText8025_BIOS();
        clear();
      } else if (ebx == 0x02) {
        SwitchTo320X200X256_BIOS();
      }
    }
  } else if (eax == 0x22) {
    // 任务API
    if (ebx == 0x03) {
      //   task->forever = 1;
    } else if (ebx == 0x04) {
      send_ipc_message(ecx, (void *)(ds_base + edx), esi, asynchronous);
    } else if (ebx == 0x05) {
      get_ipc_message((void *)(ds_base + edx), ecx);
    } else if (ebx == 0x06) {
      reg[EAX] = ipc_message_len(ecx);
    } else if (ebx == 0x07) {
      reg[EAX] = get_tid(task);
    } else if (ebx == 0x08) {
      reg[EAX] = have_msg();
    } else if (ebx == 0x09) {
      get_msg_all((void *)(ds_base + edx));
    } else if (ebx == 0x0a) {

      extern int init_ok_flag;
      init_ok_flag = 0;
      // u32 *stack = page_malloc_one() + 0x1000;
      // stack--;
      // *stack = (u32)(esi);
      // stack--;
      // *stack = (u32)(edx);
      unsigned pde = current_task()->pde;
      io_cli();
      set_cr3(PDE_ADDRESS);
      for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
        u32 *pde_entry = (u32 *)(pde + i);
        if (pages[IDX(*pde_entry)].count > 1 && !(*pde_entry & PG_SHARED)) {
          u32 old    = *pde_entry & 0xfffff000;
          u32 attr   = *pde_entry & 0xfff;
          *pde_entry = (unsigned)page_malloc_one_count_from_4gb();
          memcpy((void *)(*pde_entry), (void *)old, 0x1000);
          pages[IDX(old)].count--;
          *pde_entry |= attr;
          *pde_entry |= PG_SHARED;
        } else {
          *pde_entry |= PG_SHARED;
        }
        unsigned p = *pde_entry & (0xfffff000);
        for (int j = 0; j < 0x1000; j += 4) {
          u32 *pte_entry = (u32 *)(p + j);
          if (pages[IDX(*pte_entry)].count == 1 && (*pte_entry & PG_USU) &&
              !(*pte_entry & PG_SHARED)) {
            *pte_entry |= PG_SHARED;
          }
        }
      }
      set_cr3(pde);
      io_sti();
      logk("OK\n");
      // while (true)
      //   ;
      mtask *t      = create_task(user_thread_into, (unsigned)0, 1, 1);
      init_ok_flag  = 1;
      t->alloc_addr = task->alloc_addr;
      t->alloc_size = task->alloc_size;
      t->TTY        = current_task()->TTY;
      t->nfs        = task->nfs;
      t->ptid       = task->tid;
      t->mx         = 0;
      t->my         = 0;
      unsigned *r   = page_malloc_one_no_mark();
      r[0]          = esi;
      r[1]          = edx;

      t->line  = (char *)r;
      reg[EAX] = t->tid;
    } else if (ebx == 0x0b) {
      task_lock();
    } else if (ebx == 0x0c) {
      task_unlock();
    } else if (ebx == 0x0d) {
      task_kill(ecx);
    }
  } else if (eax == 0x23) {
    if (ebx == 0x01) {
      reg[EAX] = task->TTY->color;
    } else if (ebx == 0x02) {
      task->TTY->color = ecx;
    }
  } else if (eax == 0x24) {
    // 计时器API
    if (ebx == 0x00) {
      task->timer            = timer_alloc();
      task->timer->fifo      = (struct FIFO8 *)page_malloc(sizeof(struct FIFO8));
      task->timer->fifo->buf = (u8 *)page_malloc(50 * sizeof(u8));
      fifo8_init(task->timer->fifo, 50, task->timer->fifo->buf);
      timer_init(task->timer, task->timer->fifo, 1);
    } else if (ebx == 0x01) {
      timer_settime(task->timer, ecx);
    } else if (ebx == 0x02) {
      if (fifo8_get(task->timer->fifo) == 1) {
        reg[EAX] = 1;
      } else {
        reg[EAX] = 0;
      }
    } else if (ebx == 0x03) {
      page_free((void *)task->timer->fifo->buf, 50 * sizeof(u8));
      page_free((void *)task->timer->fifo, sizeof(struct FIFO8));
      timer_free(task->timer);
    }
  } else if (eax == 0x25) {
    reg[EAX] = vfs_format(ebx, (char *)(ecx + ds_base));
  } else if (eax == 0x26) {
    // CMOS时间
    if (ebx == 0x00) {
      reg[EAX] = get_hour_hex();
    } else if (ebx == 0x01) {
      reg[EAX] = get_min_hex();
    } else if (ebx == 0x02) {
      reg[EAX] = get_sec_hex();
    } else if (ebx == 0x03) {
      reg[EAX] = get_day_of_month();
    } else if (ebx == 0x04) {
      reg[EAX] = get_day_of_week();
    } else if (ebx == 0x05) {
      reg[EAX] = get_mon_hex();
    } else if (ebx == 0x06) {
      reg[EAX] = get_year();
    }
  } else if (eax == 0x27) {
    if (running_mode == POWERINTDOS) {
      struct VBEINFO *vbe = (struct VBEINFO *)VBEINFO_ADDRESS;
      SDraw_Px((vram_t *)vbe->vram, ebx, ecx, edx, vbe->xsize);
    }
  } else if (eax == 0x28) {
    if (running_mode == POWERINTDOS) {
      struct VBEINFO *vbe = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t         *r   = (vram_t *)vbe->vram;
      reg[EAX]            = r[ebx * vbe->xsize + ecx];
    }
  } else if (eax == 0x29) {
    if (running_mode == POWERINTDOS) {
      struct VBEINFO *vbe = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t         *r   = (vram_t *)vbe->vram;
      memcpy((void *)(ebx + ds_base), r, vbe->xsize * vbe->ysize * 4);
    }
  } else if (eax == 0x2a) {
    if (running_mode == POWERINTDOS) {
      struct VBEINFO *vbe    = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t         *r      = (vram_t *)vbe->vram;
      int             x      = ebx;
      int             y      = ecx;
      int             w      = edx;
      int             h      = esi;
      u32            *buffer = (u32 *)(edi + ds_base);
      for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
          r[j * vbe->xsize + i] = buffer[(j - y) * w + (i - x)];
        }
      }
    }
  } else if (eax == 0x2b) {
    if (running_mode == POWERINTDOS) {
      int a, c;
      a                           = 0;
      c                           = ebx;
      struct VBEINFO *vbe         = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t         *vram_buffer = (vram_t *)vbe->vram;
      for (; c <= vbe->ysize; c++, a++) {
        for (int i = 0; i < vbe->xsize; i++) {
          vram_buffer[a * vbe->xsize + i] = vram_buffer[c * vbe->xsize + i];
        }
      }
      SDraw_Box(vram_buffer, 0, a, vbe->xsize, vbe->ysize, 0x0, vbe->xsize);
    }
  } else if (eax == 0x2c) {
    if (running_mode == POWERINTDOS) {
      struct VBEINFO *vbe         = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t         *vram_buffer = (vram_t *)vbe->vram;
      (void)(vram_buffer);
      SDraw_Box((vram_t *)vbe->vram, ebx, ecx, edx, esi, edi, vbe->xsize);
    }
  } else if (eax == 0x2d) {
    reg[EAX] = UTCTimeStamp(get_year(), get_mon_hex(), get_day_of_month(), get_hour_hex(),
                            get_min_hex(), get_sec_hex());
  } else if (eax == 0x2e) {
    gettime_ns((time_ns_t *)edi);
  } else if (eax == 0x2f) {
    task->fpu_flag = 0;
  } else if (eax == 0x30) {
    current_task()->Pkeyfifo = malloc(sizeof(struct FIFO8));
    current_task()->Ukeyfifo = malloc(sizeof(struct FIFO8));
    u8 *kbuf                 = (u8 *)page_malloc(4096);
    u8 *mbuf                 = (u8 *)page_malloc(4096);
    fifo8_init(current_task()->Pkeyfifo, 4096, kbuf);
    fifo8_init(current_task()->Ukeyfifo, 4096, mbuf);
    current_task()->keyboard_press   = kbd_press;
    current_task()->keyboard_release = kbd_up;
  } else if (eax == 0x31) {
    reg[EAX] = fifo8_status(current_task()->Pkeyfifo);
  } else if (eax == 0x32) {
    reg[EAX] = fifo8_status(current_task()->Ukeyfifo);
  } else if (eax == 0x33) {
    reg[EAX] = fifo8_get(current_task()->Pkeyfifo);
  } else if (eax == 0x34) {
    reg[EAX] = fifo8_get(current_task()->Ukeyfifo);
  } else if (eax == 0x35) {
    unsigned start_addr = task->alloc_addr + *(task->alloc_size);
    for (int i = 0; i < (ebx / 0x1000); i++) {
      //  logk("LINK %08x\n", start_addr + (i) * 0x1000);
      page_link_share(start_addr + (i)*0x1000);
    }
    // page_links(start_addr,ebx / 0x1000);
    *(task->alloc_size) += ebx;
  } else if (eax == 0x36) {
    char *s = env_read(ebx + ds_base);
    if (s) {
      strcpy(ecx + ds_base, s);
      reg[EAX] = 1;
    } else {
      reg[EAX] = 0;
    }
  } else if (eax == 0x37) {
    vfs_getPath_no_drive(ebx + ds_base);
  } else if (eax == 0x38) {
    reg[EAX] = task->nfs->drive;
  } else if (eax == 0x39) {
    reg[EAX] = os_execute(ebx, ecx);
  } else if (eax == 0x3a) {
    clear();
  } else if (eax == 0x3b) {
    reg[EAX] = vfs_check_mount(ebx);
  } else if (eax == 0x3c) {
    reg[EAX] = vfs_mount_disk(ebx, ecx);
  } else if (eax == 0x3d) {
    vfs_change_disk(ebx);
  } else if (eax == 0x3e) {
    reg[EAX] = memsize;
  } else if (eax == 0x3f) {
    extern struct PAGE_INFO *pages;
    u32                      r = 0;
    for (int i = 0; i < div_round_up(memsize, 0x1000); i++) {
      if (pages[i].count) { r++; }
    }
    reg[EAX] = r;
  } else if (eax == 0x40) {
    reg[EAX] = vfs_delfile((char *)(edx + ds_base));
  } else if (eax == 0x41) {
    reg[EAX] = vfs_change_path((char *)(edx + ds_base));
  } else if (eax == 0x42) {
    tty_start_curor_moving(task->TTY);
  } else if (eax == 0x43) {
    tty_stop_cursor_moving(task->TTY);
  } else if (eax == 0x44) {
    vfs_unmount_disk(ebx);
  } else if (eax == 0x45) {
    reg[EAX] = task->TTY->xsize;
  } else if (eax == 0x46) {
    reg[EAX] = task->TTY->ysize;
  } else if (eax == 0x47) {
    vfs_renamefile(ebx, ecx);
  } else if (eax == 0x48) {
    logk(ebx);
  } else if (eax == 0x49) {
    unsigned old = current_task()->handler[ebx];
    set_signal_handler(ebx, ecx);
    reg[EAX] = old;
  } else if (eax == 0x4a) {
    reg[EAX] = task_fork();
  } else if (eax == 0x4b) {
    reg[EAX] = waittid(ebx);
  } else if (eax == 0x4c) {
    extern unsigned m_eip, m_cr3;
    m_eip = reg[EBX];
    m_cr3 = current_task()->pde;
  } else if (eax == 0x4d) {
    mouse_ready(&mdec);
  } else if (eax == 0x4e) {
    unsigned i;
    i        = fifo8_status(task_get_mouse_fifo(task));
    reg[EAX] = i;
  } else if (eax == 0x4f) {
    reg[EAX] = fifo8_get(task_get_mouse_fifo(task));
  } else if (eax == 0x50) {
    if (current_task()->ready == 0) {
      io_cli();
      //      current_task()->timeout = 1;
      task_next();
      io_sti();
    } else {
      current_task()->ready = 0;
    }
  } else if (eax == 0x51) {
    reg[EAX] = fartty_alloc(ebx, ecx, current_task()->pde, edx, esi);
  } else if (eax == 0x52) {
    tty_set(get_task(ebx), ecx);
  } else if (eax == 0x53) {
    tty_free(ebx);
  } else if (eax == 0x54) {
    current_task()->ret_to_app = ebx;
  } else if (eax == 0x55) {
    extern int disable_flag;
    disable_flag = 1;
    extern mtask *keyboard_use_task;
    keyboard_use_task = task;
  } else if (eax == 0x56) {
    if (!custom_handler) {
      custom_handler       = ebx;
      custom_handler_pde   = current_task()->pde;
      custom_handler_owner = current_task();
    }
  } else if (eax == 0x57) {
    unsigned a1    = reg[EBX] & 0xfffff000;
    unsigned sz    = reg[ECX];
    unsigned a_pde = reg[EDX];
    unsigned b1    = reg[ESI] & 0xfffff000;
    unsigned b_pde = reg[EDI];
    unsigned count = div_round_up(sz, 0x1000);
    io_cli();
    for (int i = 0; i < count; i++) {
      unsigned paddr;
      paddr = page_get_phy_pde(b1 + i * 0x1000, b_pde);
      page_set_physics_attr_pde(a1 + i * 0x1000, paddr, 7,
                                a_pde); // PG_P | PG_USU | PG_RWW
    }
    io_sti();
  } else if (eax == 0x58) {
    io_cli();
    mtask *t = get_task(ebx);
    if (t->urgent) {
      io_sti();
      return;
    }
    t->urgent  = 1;
    t->timeout = 5;
    t->running = 0;
    io_sti();
  } else if (eax == 0x59) {
    io_cli();
    mtask *t   = get_task(ebx);
    t->timeout = 1;
    t->running = 0;
    t->urgent  = 0;
    io_sti();
  }
  return;
}

void custom_inthandler(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax) {
  unsigned *alloc_sz = current_task()->alloc_size;
  unsigned  alloc_ar = current_task()->alloc_addr;
  unsigned  tid      = current_task()->tid;
  if (!custom_handler) return;

  current_task()->alloc_size = custom_handler_owner->alloc_size;
  current_task()->alloc_addr = custom_handler_owner->alloc_addr;
  current_task()->tid        = custom_handler_owner->tid;
  int     *reg               = &eax + 1; /* eax后面的地址*/
  unsigned args[]            = {
      edi, esi, ebp, esp, ebx, edx, ecx, eax, custom_handler_pde, current_task()->pde, tid};
  if (ebx) {
    char *s1 = malloc(strlen(ebx) + 1);
    memcpy(s1, ebx, strlen(ebx) + 1);
    args[EBX] = s1;
  }
  call_across_page(custom_handler, custom_handler_pde, args);
  if (ebx) free(args[EBX]);
  args[EBX] = ebx;
  for (int i = 0; i < 8; i++) {
    reg[i] = args[i];
  }
  current_task()->alloc_size = alloc_sz;
  current_task()->alloc_addr = alloc_ar;
  current_task()->tid        = tid;
}