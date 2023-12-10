#include <dos.h>
#define IDX(addr) ((unsigned)addr >> 12)            // 获取 addr 的页索引
#define DIDX(addr) (((unsigned)addr >> 22) & 0x3ff) // 获取 addr 的页目录索引
#define TIDX(addr) (((unsigned)addr >> 12) & 0x3ff) // 获取 addr 的页表索引
#define PAGE(idx) ((unsigned)idx << 12) // 获取页索引 idx 对应的页开始的位置
extern struct PAGE_INFO *pages;
void kbd_press(uint8_t dat, uint32_t task) {
  fifo8_put(get_task(task)->Pkeyfifo, dat);
}
void kbd_up(uint8_t dat, uint32_t task) {
  fifo8_put(get_task(task)->Ukeyfifo, dat);
}
void user_thread_into(unsigned eip, unsigned esp) {
  while (!current_task()->line)
    ; // 等待配置
  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf = (char *)page_malloc_one();
  char *mbuf = (char *)page_malloc_one();
  fifo8_init((struct FIFO8 *)kfifo, 4096, (unsigned char *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (unsigned char *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
  char tmp[100];
  task_to_user_mode(eip, esp);
  for (;;) {
  }
}
void *malloc_app_heap(void *alloc_addr, uint32_t ds_base, uint32_t size) {
  memory *mem = (memory *)alloc_addr;
  mem->freeinf = (freeinfo *)((char *)mem->freeinf + ds_base);
  freeinfo *fi;
  for (fi = mem->freeinf; fi->next != NULL; fi = fi->next) {
    fi->next = (freeinfo *)((char *)fi->next + ds_base);
    fi->f = (free_member *)((char *)fi->f + ds_base);
  }
  fi->f = (free_member *)((char *)fi->f + ds_base);
  char *p = (char *)mem_alloc(mem, size + sizeof(int)) + ds_base;
  *(int *)p = size;
  p += sizeof(int);
  fi->f = (free_member *)((char *)fi->f - ds_base);
  for (fi = mem->freeinf; ((freeinfo *)((char *)fi + ds_base))->next != NULL;
       fi = ((freeinfo *)((char *)fi + ds_base))->next) {
    fi->next = (freeinfo *)((char *)fi->next - ds_base);
    fi->f = (free_member *)((char *)fi->f - ds_base);
  }
  mem->freeinf = (freeinfo *)((char *)mem->freeinf - ds_base);
  return (void *)p;
}
void free_app_heap(void *alloc_addr, uint32_t ds_base, void *p) {
  memory *mem = (memory *)alloc_addr;
  mem->freeinf = (freeinfo *)((char *)mem->freeinf + ds_base);
  freeinfo *fi;
  for (fi = mem->freeinf; fi->next != NULL; fi = fi->next) {
    fi->next = (freeinfo *)((char *)fi->next + ds_base);
    fi->f = (free_member *)((char *)fi->f + ds_base);
  }
  fi->f = (free_member *)((char *)fi->f + ds_base);
  uint32_t size = ((int *)p)[-1] + sizeof(int);
  mem_free(mem, (void *)(&((int *)p)[-1] - ds_base), size);
  fi->f = (free_member *)((char *)fi->f - ds_base);
  for (fi = mem->freeinf; ((freeinfo *)((char *)fi + ds_base))->next != NULL;
       fi = ((freeinfo *)((char *)fi + ds_base))->next) {
    fi->next = (freeinfo *)((char *)fi->next - ds_base);
    fi->f = (free_member *)((char *)fi->f - ds_base);
  }
  mem->freeinf = (freeinfo *)((char *)mem->freeinf - ds_base);
  return;
}
enum { EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX };
void inthandler36(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx,
                  int eax) {
  // PowerintDOS API
  mtask *task = current_task();
  int ds_base = 0;
  void *alloc_addr = task->alloc_addr;
  int alloc_size = task->alloc_size;
  memory *current_mm = task->mm;
  int *reg = &eax + 1; /* eax后面的地址*/
                       /*强行改写通过PUSHAD保存的值*/
  /* reg[0] : EDI,   reg[1] : ESI,   reg[2] : EBP,   reg[3] : ESP */
  /* reg[4] : EBX,   reg[5] : EDX,   reg[6] : ECX,   reg[7] : EAX */
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
        PrintChineseStr(ecx, edx, edi, (unsigned char *)esi + ds_base);
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
    Text_Draw_Box(ecx, ebx, esi, edx, (unsigned char)edi);
  } else if (eax == 0x0e) {
    reg[ECX] = get_y();
    reg[EDX] = get_x();
  } else if (eax == 0x0d) {
    beep(ebx, ecx, edx);
  } else if (eax == 0x0f) {
    if (running_mode == POWERINTDOS) {
      mtask *task = current_task();
      int i, mx1 = task->mx, my1 = task->my, bufx = task->mx * 8,
             bufy = task->my * 16;
      int bx = mx1;
      int by = my1;
      int bmp =
          *(char *)(task->TTY->vram + by * task->TTY->xsize * 2 + bx * 2 + 1);
      mouse_ready(&mdec);
      for (;;) {
        if (fifo8_status(task_get_mouse_fifo(task)) == 0) {
          io_stihlt();
        } else {
          i = fifo8_get(task_get_mouse_fifo(task));
          if (mouse_decode(&mdec, i) != 0) {
            if (task->TTY != now_tty() && task->TTY->using1 == 1) {
              continue;
            }
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
              reg[ECX] = task->mx;
              int alloc_size = task->alloc_size;
              reg[EDX] = task->my;
              reg[ESI] = 3;
              break;
            } else if (mdec.roll != MOUSE_ROLL_NONE) {
              reg[ECX] = task->mx;
              reg[EDX] = task->my;
              reg[ESI] = 3 + mdec.roll;
              break;
            }
            mx1 = task->mx;
            my1 = task->my;
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
            task->mx = bufx / 8;
            task->my = bufy / 16;
            *(char *)(task->TTY->vram + my1 * task->TTY->xsize * 2 + mx1 * 2 +
                      1) = bmp;
            bmp = *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 +
                            task->mx * 2 + 1);
            *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 +
                      task->mx * 2 + 1) = ~bmp;
            //  mouse_sleep(&mdec);
            //  sleep(50);
            mouse_ready(&mdec);
          }
        }
      }
      mouse_sleep(&mdec);
      *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 +
                task->mx * 2 + 1) = bmp;
      task->mx = mx1;
      task->my = my1;
    }
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
    logk("c: %08x\n", task->pde);
    command_run((char *)(edx + ds_base));
    asm("xchg %bx,%bx");
    logk("n: %08x\n", task->pde);
    logk("--------------------------------\n");
  } else if (eax == 0x1a) {
    if (ebx == 0x01) {
      int fsize = vfs_filesize((char *)(edx + ds_base));
      reg[EDX] = fsize;
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
      reg[EAX] = vfs_createfile(FilePath);
    } else if (ebx == 0x04) {
      char *FilePath = (char *)(ds_base + edx);
      reg[EAX] = vfs_createdict(FilePath);
    } else if (ebx == 0x05) {
      char *FilePath = (char *)(ds_base + edx);
      char *Ptr = (char *)ds_base + esi;
      int length = ecx;
      int offset = edi;
      logk("EDIT %s\n", FilePath);
      EDIT_FILE(FilePath, Ptr, length, offset);
    } else if (ebx == 0x06) {
      char *Path = (char *)(ds_base + edx);
      struct List *file_list = vfs_listfile(Path);
      int number;
      for (number = 1; FindForCount(number, file_list) != NULL; number++)
        ;
      char *p = (char *)malloc_app_heap(alloc_addr, ds_base,
                                        number * sizeof(vfs_file));
      for (int i = 1; FindForCount(i, file_list) != NULL; i++) {
        memcpy((void *)(p + sizeof(vfs_file) * (i - 1)),
               (void *)FindForCount(i, file_list)->val, sizeof(vfs_file));
        free((void *)FindForCount(i, file_list)->val);
      }
      memset((void *)(p + (number - 1) * sizeof(vfs_file)), 0,
             sizeof(vfs_file));
      DeleteList(file_list);
      reg[EAX] = (int)p - ds_base;
    }
  } else if (eax == 0x1b) {
    int i;
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
    if (!(*(unsigned char *)(0xf0000000))) {
      logk("here\n");
      while (FindForCount(1, vfs_now->path) != NULL) {
        page_free(FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val,
                  255);
        DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
        extern mtask *mouse_use_task;
        if (mouse_use_task == task) {
          mouse_sleep(&mdec);
        }
      }
      DeleteList(vfs_now->path);
      free(vfs_now->cache);
      page_free((void *)vfs_now, sizeof(vfs_t));
    } else {
      get_task(task->ptid)->nfs = task->nfs;
    }
    //  for(;;);
    task_kill(task->tid);
    for (;;)
      ;
  } else if (eax == 0x20) {
    // VBE驱动API
    if (running_mode == POWERINTDOS) {
      if (ebx == 0x01) {
        reg[EAX] = SwitchVBEMode(ecx);
      } else if (ebx == 0x02) {
        reg[EAX] = check_vbe_mode(ecx, (struct VBEINFO *)VBEINFO_ADDRESS);
      } else if (ebx == 0x05) {
        reg[EAX] = set_mode(ecx, edx, 32);
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
    //任务API
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
      unsigned int *stack = page_malloc_one() + 0x1000;
      stack--;
      *stack = (unsigned int)(esi);
      stack--;
      *stack = (unsigned int)(edx);
      mtask *t = create_task(user_thread_into, (unsigned)stack - 4, 1, 1);
      init_ok_flag = 1;
      t->alloc_addr = task->alloc_addr;
      t->alloc_size = task->alloc_size;
      t->TTY = current_task()->TTY;
      t->nfs = task->nfs;
      t->ptid = task->tid;
      for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
        unsigned int *pde_entry = (unsigned int *)(current_task()->pde + i);
        unsigned p = *pde_entry & (0xfffff000);
        for (int j = 0; j < 0x1000; j += 4) {
          unsigned int *pte_entry = (unsigned int *)(p + j);
          if (pages[IDX(*pte_entry)].count)
            pages[IDX(*pte_entry)].count--;
        }
        // pages[IDX(*pde_entry)].count--;
      }
      t->line = (char *)1;
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
      io_cli();
      task->timer = timer_alloc();
      task->timer->fifo = (struct FIFO8 *)page_malloc(sizeof(struct FIFO8));
      task->timer->fifo->buf =
          (unsigned char *)page_malloc(50 * sizeof(unsigned char));
      fifo8_init(task->timer->fifo, 50, task->timer->fifo->buf);
      timer_init(task->timer, task->timer->fifo, 1);
      io_sti();
    } else if (ebx == 0x01) {
      timer_settime(task->timer, ecx);
    } else if (ebx == 0x02) {
      if (fifo8_get(task->timer->fifo) == 1) {
        reg[EAX] = 1;
      } else {
        reg[EAX] = 0;
      }
    } else if (ebx == 0x03) {
      page_free((void *)task->timer->fifo->buf, 50 * sizeof(unsigned char));
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
      vram_t *r = (vram_t *)vbe->vram;
      reg[EAX] = r[ebx * vbe->xsize + ecx];
    }
  } else if (eax == 0x29) {
    if (running_mode == POWERINTDOS) {
      struct VBEINFO *vbe = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t *r = (vram_t *)vbe->vram;
      memcpy((void *)(ebx + ds_base), r, vbe->xsize * vbe->ysize * 4);
    }
  } else if (eax == 0x2a) {
    if (running_mode == POWERINTDOS) {
      struct VBEINFO *vbe = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t *r = (vram_t *)vbe->vram;
      int x = ebx;
      int y = ecx;
      int w = edx;
      int h = esi;
      unsigned int *buffer = (unsigned int *)(edi + ds_base);
      for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
          r[j * vbe->xsize + i] = buffer[(j - y) * w + (i - x)];
        }
      }
    }
  } else if (eax == 0x2b) {
    if (running_mode == POWERINTDOS) {
      int a, c;
      a = 0;
      c = ebx;
      struct VBEINFO *vbe = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t *vram_buffer = (vram_t *)vbe->vram;
      for (; c <= vbe->ysize; c++, a++) {
        for (int i = 0; i < vbe->xsize; i++) {
          vram_buffer[a * vbe->xsize + i] = vram_buffer[c * vbe->xsize + i];
        }
      }
      SDraw_Box(vram_buffer, 0, a, vbe->xsize, vbe->ysize, 0x0, vbe->xsize);
    }
  } else if (eax == 0x2c) {
    if (running_mode == POWERINTDOS) {
      struct VBEINFO *vbe = (struct VBEINFO *)VBEINFO_ADDRESS;
      vram_t *vram_buffer = (vram_t *)vbe->vram;
      (void)(vram_buffer);
      SDraw_Box((vram_t *)vbe->vram, ebx, ecx, edx, esi, edi, vbe->xsize);
    }
  } else if (eax == 0x2d) {
    reg[EAX] = ntp_time_stamp(get_year(), get_mon_hex(), get_day_of_month(),
                              get_hour_hex(), get_min_hex(), get_sec_hex());
  } else if (eax == 0x2e) {
    reg[EAX] = timerctl.count;
  } else if (eax == 0x2f) {
    task->fpu_flag = 0;
  } else if (eax == 0x30) {
    current_task()->Pkeyfifo = malloc(sizeof(struct FIFO8));
    current_task()->Ukeyfifo = malloc(sizeof(struct FIFO8));
    unsigned char *kbuf = (unsigned char *)page_malloc(4096);
    unsigned char *mbuf = (unsigned char *)page_malloc(4096);
    fifo8_init(current_task()->Pkeyfifo, 4096, kbuf);
    fifo8_init(current_task()->Ukeyfifo, 4096, mbuf);
    current_task()->keyboard_press = kbd_press;
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
    logk("sbrk %08x\n", ebx);
    unsigned page_len = div_round_up(ebx, 0x1000);
    unsigned start_addr =
        ((task->alloc_addr + task->alloc_size - 1) & 0xfffff000);
    for (int i = 0; i < page_len; i++) {
      page_link(start_addr + (i + 1) * 0x1000);
    }
    task->alloc_size += ebx;
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
    os_execute(ebx, ecx);
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
    uint32_t r = 0;
    for (int i = 0; i < div_round_up(memsize, 0x1000); i++) {
      if (pages[i].flag == 1) {
        r++;
      }
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
  }
  return;
}
