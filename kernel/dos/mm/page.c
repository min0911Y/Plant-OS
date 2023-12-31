#include <dos.h>
#define IDX(addr) ((unsigned)addr >> 12)            // 获取 addr 的页索引
#define DIDX(addr) (((unsigned)addr >> 22) & 0x3ff) // 获取 addr 的页目录索引
#define TIDX(addr) (((unsigned)addr >> 12) & 0x3ff) // 获取 addr 的页表索引
#define PAGE(idx) ((unsigned)idx << 12) // 获取页索引 idx 对应的页开始的位置
void *page_malloc_one_no_mark();
struct PAGE_INFO *pages = (struct PAGE_INFO *)PAGE_MANNAGER;
void init_pdepte(unsigned int pde_addr, unsigned pte_addr, unsigned page_end) {

  memset((void *)pde_addr, 0, page_end - pde_addr);
  // 这是初始化PDE 页目录
  for (int addr = pde_addr, i = pte_addr | PG_P | PG_USU | PG_RWW;
       addr != pte_addr; addr += 4, i += 0x1000) {
    *(int *)(addr) = i;
  }
  // 这是初始化PTE 页表
  for (int addr = PTE_ADDRESS, i = PG_P | PG_USU | PG_RWW; addr != PAGE_END;
       addr += 4, i += 0x1000) {
    *(int *)(addr) = i;
  }
  return;
}
void init_page_manager(struct PAGE_INFO *pg) {
  memset(pg, 0, 1024 * 1024 * sizeof(struct PAGE_INFO));
} // 全部置为0就好
void page_set_alloced(struct PAGE_INFO *pg, unsigned int start,
                      unsigned int end) {
  for (int i = IDX(start); i <= IDX(end); i++) {
    pg[i].flag = 1;
    pg[i].count = 0; // 设置占用，但是没有进程引用
  }
}
// 某些设计思路：
// 从0x70000000开始，到0xf0000000
// 大概2GB的内存可以给应用程序分配，OS使用前0x70000000的内存地址
// 为了防止应用程序和操作系统抢占前0x70000000的内存，所以page_link和copy_on_write是从后往前分配的
// OS应该是用不完0x70000000的，所以应用程序大概是可以用满2GB
unsigned pde_clone(unsigned addr) {
  for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
    unsigned int *pde_entry = (unsigned int *)(addr + i);
    unsigned p = *pde_entry & (0xfffff000);
    for (int j = 0; j < 0x1000; j += 4) {
      unsigned int *pte_entry = (unsigned int *)(p + j);

      if (pages[IDX(*pte_entry)].count) {
        pages[IDX(*pte_entry)].count++;
      }
      *pte_entry &= ~PG_RWW;
    }
    if (pages[IDX(*pde_entry)].count)
      pages[IDX(*pde_entry)].count++;
    *pde_entry &= ~PG_RWW;
  }
  unsigned result = page_malloc_one_no_mark();
  memcpy(result, addr, 0x1000);
  flush_tlb(result);
  flush_tlb(addr);
  return result;
}
void pde_reset(unsigned addr) {
  for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
    unsigned int *pde_entry = (unsigned int *)(addr + i);
    unsigned p = *pde_entry & (0xfffff000);
    *pde_entry |= PG_RWW;
  }
}
void free_pde(unsigned addr) {
  if (addr == PDE_ADDRESS)
    return;
  for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
    unsigned int *pde_entry = (unsigned int *)(addr + i);
    unsigned p = *pde_entry & (0xfffff000);
    for (int j = 0; j < 0x1000; j += 4) {
      unsigned int *pte_entry = (unsigned int *)(p + j);
      if (!(*pte_entry & PG_RWW)) {
        if (pages[IDX(*pte_entry)].count > 1) {
          if(get_task(pages[IDX(*pte_entry)].task_id)->pde == addr) pages[IDX(*pte_entry)].task_id = 0; // 还有别人引用
          pages[IDX(*pte_entry)].count--;
        }
      }
    }
    if (!(*pde_entry & PG_RWW)) {
      if (pages[IDX(*pde_entry)].count > 1) {
        if(get_task(pages[IDX(*pde_entry)].task_id)->pde == addr) pages[IDX(*pde_entry)].task_id = 0; // 还有别人引用
        pages[IDX(*pde_entry)].count--;
      }
    }
  }
  flush_tlb(addr);
  page_free_one(addr);
}
// 刷新虚拟地址 vaddr 的 块表 TLB
void flush_tlb(unsigned vaddr) {
  asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}
void page_set_attr(unsigned pde, unsigned page, unsigned attr) {
  unsigned t, p;
  t = DIDX(page);
  p = TIDX(page);
  unsigned *pte = (unsigned int *)((pde + t * 4));
  unsigned *physics = (unsigned *)((*pte & 0xfffff000) + p * 4);
  *physics &= (0xfffff000);
  *physics |= attr;
  flush_tlb(page);
}
void page_link_pde(unsigned addr, unsigned pde) {
  unsigned pde_backup = current_task()->pde;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t = DIDX(addr);
  p = (addr >> 12) & 0x3ff;
  unsigned *pte = (unsigned int *)((pde + t * 4));
  // logk("*pte = %08x\n",*pte);
  if (pages[IDX(*pte)].count > 1) {
    pages[IDX(*pte)].count--;
  }
  uint32_t old = *pte & 0xfffff000;
  *pte = page_malloc_one_count_from_4gb();
  memcpy(*pte, old, 0x1000);
  *pte |= 7;
  unsigned *physics = (unsigned *)((*pte & 0xfffff000) + p * 4);
  // logk("*pte = %08x\n",*physics);
  if (pages[IDX(*physics)].count > 1) {
    pages[IDX(*physics)].count--;
  }
  *physics = page_malloc_one_count_from_4gb();
  *physics |= 7;
  flush_tlb(pte);
  flush_tlb(addr);
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
}
void page_link(unsigned addr) { page_link_pde(addr, current_task()->pde); }
void copy_from_phy_to_line(unsigned phy, unsigned line, unsigned pde,
                           unsigned size) {
  unsigned pg = div_round_up(size, 0x1000);
  for (int i = 0; i < pg; i++) {
    memcpy(page_get_phy_pde(line, pde), phy, size >= 0x1000 ? 0x1000 : size);
    size -= 0x1000;
    line += 0x1000;
    phy += 0x1000;
  }
}
void set_line_address(unsigned val, unsigned line, unsigned pde,
                      unsigned size) {
  unsigned pg = div_round_up(size, 0x1000);
  for (int i = 0; i < pg; i++) {
    memset(page_get_phy_pde(line, pde), val, size >= 0x1000 ? 0x1000 : size);
    size -= 0x1000;
    line += 0x1000;
  }
}
void page_unlink(unsigned addr) {}
void C_init_page() {
  init_pdepte(PDE_ADDRESS, PTE_ADDRESS, PAGE_END);
  init_page_manager(pages);
  page_set_alloced(pages, 0, 0xa01000);
  page_set_alloced(pages, 0xc0000000, 0xffffffff);
}
void pf_set(unsigned int memsize) {
  uint32_t *pte = (uint32_t *)PTE_ADDRESS;
  for (int i = 0; pte != (uint32_t *)PAGE_END; pte++, i++) {
    if (i >= memsize / 4096 && i <= 0xc0000000 / 4096) {
      *pte = 0;
    }
  }
}
int get_line_address(int t, int p, int o) {
  // 获取线性地址
  //  t:页目录地址
  //  p:页表地址
  //  o:页内偏移地址
  return (t << 22) + (p << 12) + o;
}
int get_page_from_line_address(int line_address) {
  int t, p, page;
  t = line_address >> 22;
  p = (line_address >> 12) & 0x3ff;
  tpo2page(&page, t, p);
  return page;
}
void page2tpo(int page, int *t, int *p) {
  *t = page / 1024;
  *p = page % 1024;
}
void tpo2page(int *page, int t, int p) { *page = (t * 1024) + p; }
void *page_malloc_one_no_mark() {
  int i;
  for (i = 0; i != 1024 * 1024; i++) {
    if (pages[i].flag == 0) {
      int t, p;
      page2tpo(i, &t, &p);
      unsigned int addr = get_line_address(t, p, 0);
      pages[i].flag = 1;
      pages[i].task_id = 0;
      pages[i].count++;
      return (void *)addr;
    }
  }
  return NULL;
}
void *page_malloc_one() {
  int i;
  for (i = 0; i != 1024 * 1024; i++) {
    if (pages[i].flag == 0) {
      int t, p;
      page2tpo(i, &t, &p);
      unsigned int addr = get_line_address(t, p, 0);
      pages[i].flag = 1;
      pages[i].task_id = get_tid(current_task());
      pages[i].count++;
      return (void *)addr;
    }
  }

  return NULL;
}
void *page_malloc_one_count_from_4gb() {
  int i = 0;
  for (i = IDX(memsize) - 1; i >= 0; i--) {
    if (pages[i].flag == 0 && pages[i].count == 0) {
      unsigned int addr = PAGE(i);
      pages[i].flag = 1;
      pages[i].task_id = get_tid(current_task());
      pages[i].count++;
      return (void *)addr;
    }
  }
  return NULL;
}
void gc(unsigned tid) {
  int i;
  for (i = 0; i < 1024 * 1024; i++) {
    if (pages[i].flag == 1 && pages[i].task_id == tid) {
      if (pages[i].count > 1) {
        pages[i].count--;
        pages[i].task_id =
            0; // 不知道接下来谁会用，但是有可能被继承这个tid的人给误删除，所以这里把task_id置为0
        continue;
      }
      // logk("free %08x\n",PAGE(i));
      page_free_one(PAGE(i));
    }
  }
}
int get_pageinpte_address(int t, int p) {
  int page;
  tpo2page(&page, t, p);
  return (PTE_ADDRESS + page * 4);
}
void page_free_one(void *p) {
  // logk("free %08x\n", p);
  if (IDX(p) >= 1024 * 1024)
    return;
  if (pages[IDX(p)].count - 1 > 0) {
    // logk("[page_free_one]: cannot free because the count of memory isn't "
    //      "0.pages[get_page_from_line_address((int)p)].count = %d\n",
    //     pages[get_page_from_line_address((int)p)].count);
    pages[IDX(p)].count--;
    return;
  }
  pages[IDX(p)].flag = 0;
  pages[IDX(p)].task_id = 0;
  pages[IDX(p)].count = 0;
}
unsigned get_shell_tid(struct TASK *task) {
  // if (task->app == 0) {
  //   return get_tid(task);
  // }
  // if (task->app == 1) {
  //   return get_shell_tid(task->thread.father);
  // }
  return 0;
}
int find_kpage(int line, int n) {
  int free = 0;
  // 找一个连续的线性地址空间
  for (; line != 1024 * 1024; line++) {
    if (pages[line].flag == 0) {
      free++;
    } else {
      free = 0;
    }
    if (free == n) {
      for (int j = line - n + 1; j != line + 1; j++) {
        pages[j].flag = 1;
        pages[j].task_id = 0x7f;
        pages[j].count++;
      }
      line -= n - 1;
      break;
    }
  }
  return line;
}
void *page_malloc(int size) {
  int n = ((size - 1) / (4 * 1024)) + 1;
  int i = find_kpage(0, n);
  int t, p;
  page2tpo(i, &t, &p);
  clean((char *)get_line_address(t, p, 0), n * 4 * 1024);
  unsigned addr = (void *)get_line_address(t, p, 0);
  return addr;
}
void page_free(void *p, int size) {
  int n = ((size - 1) / (4 * 1024)) + 1;
  p = (int)p & 0xfffff000;
  for (int i = 0; i < n; i++) {
    page_free_one((void *)p);
    p += 0x1000;
  }
}
void *get_phy_address_for_line_address(void *line) {
  int t, p;
  page2tpo(get_page_from_line_address((int)line), &t, &p);
  return (void *)(*(int *)get_pageinpte_address(t, p));
}
void set_phy_address_for_line_address(void *line, void *phy) {
  int t, p;
  page2tpo(get_page_from_line_address((int)line), &t, &p);
  *(int *)get_pageinpte_address(t, p) = (int)phy;
}
// 映射地址
void page_map(void *target, void *start, void *end) {
  target = (void *)((int)target & 0xfffff000);
  start = (void *)((int)start & 0xfffff000);
  end = (void *)((int)end & 0xfffff000);
  uint32_t n = (int)end - (int)start;
  n /= 4 * 1024;
  n++;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t tmp = (uint32_t)get_phy_address_for_line_address(
        (void *)((uint32_t)target + i * 4 * 1024));
    uint32_t tmp2 = (uint32_t)get_phy_address_for_line_address(
        (void *)((uint32_t)start + i * 4 * 1024));
    set_phy_address_for_line_address((void *)((uint32_t)target + i * 4 * 1024),
                                     (void *)tmp2);
    set_phy_address_for_line_address((void *)((uint32_t)start + i * 4 * 1024),
                                     (void *)tmp);
  }
}
void change_page_task_id(int task_id, void *p, unsigned int size) {
  int page = get_page_from_line_address((int)p);
  for (int i = 0; i != ((size - 1) / (4 * 1024)) + 1; i++) {
    pages[page + i].task_id = task_id;
  }
}
void showPage() {
  uint32_t *pte = (uint32_t *)PTE_ADDRESS;
  // printk("size = %d", sizeof(struct PTE_page_table));
  for (int i = 0; pte != (uint32_t *)PAGE_END; pte++, i++) {
    printk("LINE ADDRESS: %08x PHY ADDRESS: %08x P=%d RW=%d US=%d USING=%d "
           "TASK=%d\n",
           i * 4096, (*pte >> 12) << 12, ((*pte) << 31) >> 31,
           ((*pte) << 30) >> 31, ((*pte) << 29) >> 31, pages[i].flag,
           pages[i].task_id);
    //*pte &= 0xffffffff-1;
  }
}
unsigned int get_cr2() {
  unsigned r;
  //asm volatile("xchg %bx,%bx");
  asm volatile("mov %%cr2,%0" : "=r"(r));
  return r;
}
uint32_t page_get_attr_pde(unsigned vaddr, unsigned pde) {
  pde += (unsigned)(DIDX(vaddr) * 4);
  void *pte = (*(unsigned int *)pde & 0xfffff000);
  pte += (unsigned)(TIDX(vaddr) * 4);
  return (*(unsigned int *)pte) & 0x00000fff;
}
uint32_t page_get_attr(unsigned vaddr) {
  return page_get_attr_pde(vaddr, current_task()->pde);
}

uint32_t page_get_phy_pde(unsigned vaddr, unsigned pde) {
  pde += (unsigned)(DIDX(vaddr) * 4);
  void *pte = (*(unsigned int *)pde & 0xfffff000);
  pte += (unsigned)(TIDX(vaddr) * 4);
  return (*(unsigned int *)pte) & 0xfffff000;
}
uint32_t page_get_phy(unsigned vaddr) {
  return page_get_phy_pde(vaddr, current_task()->pde);
}
void copy_on_write(uint32_t vaddr) {
  void *pd = current_task()->pde;
  void *pde = pd + (unsigned)(DIDX(vaddr) * 4);
  void *pde_phy = (*(unsigned int *)(pde)&0xfffff000);

  if (!(*(unsigned *)pde & PG_RWW)) {
    unsigned backup = *(unsigned int *)(pde);
    if (pages[IDX(backup)].count < 2) {
      *(unsigned *)pde |= PG_RWW;
      goto PDE_FLUSH;
    }
    *(unsigned int *)(pde) = page_malloc_one_count_from_4gb();
    memcpy(*(unsigned int *)pde, pde_phy, 0x1000);
    *(unsigned int *)(pde) |= (backup & 0x00000fff) | PG_RWW;
    pages[IDX(backup)].count--;
  PDE_FLUSH:
    flush_tlb(pde);
  }
  void *pt = (*(unsigned int *)pde & 0xfffff000);
  void *pte = pt + (unsigned)(TIDX(vaddr) * 4);
  if (!(*(unsigned *)pte & PG_RWW)) {
    if (pages[IDX(*(unsigned *)pte)].count < 2) {
      *(unsigned *)pte |= PG_RWW;
      goto FLUSH;
    }
    // 获取旧页信息
    unsigned int old_pte = *(unsigned int *)pte;
    void *phy = (void *)(old_pte & 0xfffff000);

    // 分配一个页
    void *new_page = page_malloc_one_count_from_4gb();
    memcpy(new_page, phy, 0x1000);

    // 获取原先页的属性
    unsigned int attr = old_pte & 0x00000fff;

    // 设置PWU
    attr = attr | PG_RWW;

    // 计算新PTE
    unsigned int new_pte = 0;
    new_pte |= ((unsigned int)new_page) & 0xfffff000;
    new_pte |= attr;

    // 设置并更新
    pages[IDX(old_pte)].count--;
    *(unsigned int *)pte = new_pte;
  FLUSH:
    flush_tlb(pte);
  }
  for (int i = 0; i < 100; i++) {
    flush_tlb(vaddr);
  }
}
void page_set_physics_attr(uint32_t vaddr, void *paddr, uint32_t attr) {
  void *pde = current_task()->pde;
  pde += (unsigned)(DIDX(vaddr) * 4);
  void *pte = (*(unsigned int *)pde & 0xfffff000);
  pte += (unsigned)(TIDX(vaddr) * 4);
  void *phy = (*(unsigned int *)pte & 0xfffff000);
  *(unsigned int *)pte = paddr;
  *(unsigned int *)pte |= attr;
  flush_tlb(vaddr & 0xfffff000);
}
void PF(unsigned edi, unsigned esi, unsigned ebp, unsigned esp, unsigned ebx,
        unsigned edx, unsigned ecx, unsigned eax, unsigned gs, unsigned fs,
        unsigned es, unsigned ds, unsigned error, unsigned eip, unsigned cs,
        unsigned eflags) {
  unsigned pde = current_task()->pde;
  set_cr3(PDE_ADDRESS);
  void *line_address = (void *)get_cr2();
  if (!(page_get_attr(line_address) & PG_P)) {
    io_cli();
    printk("Fatal error: Attempt to read/write a non-existent memory %08x at "
           "%08x. System "
           "halt \n   --- at PF()",
           line_address, eip);
    asm volatile("hlt");
    for (;;)
      ;
  }
  copy_on_write(line_address);
  set_cr3(pde);
  return;
}
