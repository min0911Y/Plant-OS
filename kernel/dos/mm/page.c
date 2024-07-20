#include <dos.h>
#define IDX(addr)  ((unsigned)addr >> 12)           // 获取 addr 的页索引
#define DIDX(addr) (((unsigned)addr >> 22) & 0x3ff) // 获取 addr 的页目录索引
#define TIDX(addr) (((unsigned)addr >> 12) & 0x3ff) // 获取 addr 的页表索引
#define PAGE(idx)  ((unsigned)idx << 12) // 获取页索引 idx 对应的页开始的位置

void    *page_malloc_one_no_mark();
void     flush_tlb(unsigned vaddr);
unsigned div_round_up(unsigned num, unsigned size);

struct PAGE_INFO *pages = (struct PAGE_INFO *)PAGE_MANNAGER;

void init_pdepte(u32 pde_addr, unsigned pte_addr, unsigned page_end) {
  memset((void *)pde_addr, 0, page_end - pde_addr);
  // 这是初始化PDE 页目录
  for (int addr = pde_addr, i = pte_addr | PG_P | PG_RWW; addr != pte_addr;
       addr += 4, i += 0x1000) {
    *(int *)(addr) = i;
  }
  // 这是初始化PTE 页表
  for (int addr = PTE_ADDRESS, i = PG_P | PG_RWW; addr != PAGE_END; addr += 4, i += 0x1000) {
    *(int *)(addr) = i;
  }
  return;
}
void init_page_manager(struct PAGE_INFO *pg) {
  memset(pg, 0, 1024 * 1024 * sizeof(struct PAGE_INFO));
} // 全部置为0就好
void page_set_alloced(struct PAGE_INFO *pg, u32 start, u32 end) {
  for (int i = IDX(start); i <= IDX(end); i++) {
    pg[i].count++; // 设置占用，但是没有进程引用
  }
}
// 某些设计思路：
// 从0x70000000开始，到0xf0000000
// 大概2GB的内存可以给应用程序分配，OS使用前0x70000000的内存地址
// 为了防止应用程序和操作系统抢占前0x70000000的内存，所以page_link和copy_on_write是从后往前分配的
// OS应该是用不完0x70000000的，所以应用程序大概是可以用满2GB
unsigned pde_clone(unsigned addr) {
  for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
    u32     *pde_entry = (u32 *)(addr + i);
    unsigned p         = *pde_entry & (0xfffff000);
    pages[IDX(*pde_entry)].count++;
    *pde_entry &= ~PG_RWW;
    for (int j = 0; j < 0x1000; j += 4) {
      u32 *pte_entry = (u32 *)(p + j);
      if (!(*pde_entry & PG_USU) && !(*pde_entry & PG_P)) { continue; }
      if ((page_get_attr(get_line_address(i / 4, j / 4, 0)) & PG_USU)) {
        pages[IDX(*pte_entry)].count++;
        if (page_get_attr(get_line_address(i / 4, j / 4, 0)) & PG_SHARED) {
          *pte_entry |= PG_RWW;
          continue;
        }
      }
    }
  }
  unsigned result = (unsigned)page_malloc_one_no_mark();
  memcpy((void *)result, (void *)addr, 0x1000);
  flush_tlb(result);
  flush_tlb(addr);
  set_cr3(addr);

  return result;
}
void pde_reset(unsigned addr) {
  for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
    u32 *pde_entry  = (u32 *)(addr + i);
    *pde_entry     |= PG_RWW;
  }
}

void free_pde(unsigned addr) {
  if (addr == PDE_ADDRESS) return;
  for (int i = DIDX(0x70000000) * 4; i < DIDX(0xf1000000) * 4; i += 4) {
    u32     *pde_entry = (u32 *)(addr + i);
    unsigned p         = *pde_entry & (0xfffff000);
    if (!(*pde_entry & PG_USU) && !(*pde_entry & PG_P)) { continue; }
    for (int j = 0; j < 0x1000; j += 4) {
      u32 *pte_entry = (u32 *)(p + j);
      if (*pte_entry & PG_USU && *pte_entry & PG_P) { pages[IDX(*pte_entry)].count--; }
    }

    pages[IDX(*pde_entry)].count--;
  }
  flush_tlb(addr);
  page_free_one((void *)addr);
}
// 刷新虚拟地址 vaddr 的 块表 TLB
void flush_tlb(unsigned vaddr) {
  asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

void page_link_pde(unsigned addr, unsigned pde) {

  unsigned pde_backup = current_task()->pde;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t             = DIDX(addr);
  p             = (addr >> 12) & 0x3ff;
  unsigned *pte = (u32 *)((pde + t * 4));

  if (pages[IDX(*pte)].count > 1) {
    // 这个页目录还有人引用，所以需要复制
    pages[IDX(*pte)].count--;
    u32 old = *pte & 0xfffff000;
    *pte    = (unsigned)page_malloc_one_count_from_4gb();
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte |= 7;
  } else {
    *pte |= 7;
  }

  unsigned *physics = (unsigned *)((*pte & 0xfffff000) + p * 4); // PTE页表
  // COW
  if (pages[IDX(*physics)].count > 1) { pages[IDX(*physics)].count--; }
  *physics  = (unsigned)page_malloc_one_count_from_4gb();
  *physics |= 7;
  flush_tlb((unsigned)pte);
  flush_tlb(addr);
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
}
void page_link_pde_share(unsigned addr, unsigned pde) {

  unsigned pde_backup = current_task()->pde;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t             = DIDX(addr);
  p             = (addr >> 12) & 0x3ff;
  unsigned *pte = (u32 *)((pde + t * 4));

  if (pages[IDX(*pte)].count > 1 && !(*pte & PG_SHARED)) {
    // 这个页目录还有人引用，所以需要复制
    pages[IDX(*pte)].count--;
    u32 old = *pte & 0xfffff000;
    *pte    = (unsigned)page_malloc_one_count_from_4gb();
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte |= 7;
  } else {
    *pte |= 7;
  }

  unsigned *physics = (unsigned *)((*pte & 0xfffff000) + p * 4); // PTE页表
  // COW
  if (pages[IDX(*physics)].count > 1) { pages[IDX(*physics)].count--; }
  int flag = 0;
  if (*physics & PG_SHARED) {
    logk("THIS\n");
    flag = 1;
  }
  *physics  = (unsigned)page_malloc_one_count_from_4gb();
  *physics |= 7;
  if (flag) { *physics |= PG_SHARED; }
  flush_tlb((unsigned)pte);
  flush_tlb(addr);
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
}
void page_link_pde_paddr(unsigned addr, unsigned pde, unsigned *paddr1, unsigned paddr2) {
  unsigned pde_backup = current_task()->pde;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t             = DIDX(addr);
  p             = (addr >> 12) & 0x3ff;
  unsigned *pte = (u32 *)((pde + t * 4));
  // logk("*pte = %08x\n",*pte);
  if (pages[IDX(*pte)].count > 1 && !(*pte & PG_SHARED)) {
    int flag;
    if (*pte & PG_SHARED) flag = 1;
    pages[IDX(*pte)].count--;
    u32 old = *pte & 0xfffff000;
    *pte    = *paddr1;
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte    |= 7;
    *paddr1  = 0;
    if (flag) { *pte |= PG_SHARED; }
  } else {
    *pte |= 7;
  }

  unsigned *physics = (unsigned *)((*pte & 0xfffff000) + p * 4);
  if (pages[IDX(*physics)].count > 1) { pages[IDX(*physics)].count--; }
  int flag = 0;
  if (*physics & PG_SHARED) { flag = 1; }
  *physics  = paddr2;
  *physics |= 7;
  if (flag) { *physics |= PG_SHARED; }
  flush_tlb((unsigned)pte);
  flush_tlb(addr);
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
}
void page_links_pde(unsigned start, unsigned numbers, unsigned pde) {
  int      i     = 0;
  int      times = 0;
  unsigned a[2];
  int      j = 0;
  for (i = IDX(memsize) - 1; i >= 0 && times < numbers; i--) {
    if (pages[i].count == 0) {
      u32 addr         = PAGE(i);
      pages[i].task_id = get_tid(current_task());
      pages[i].count++;
      a[j++] = addr;
    }
    if (j == 2) {
      page_link_pde_paddr(start, pde, &(a[0]), a[1]);
      times++;
      start += 0x1000;
      j      = 0;
      if (a[0] != 0) { j = 1; }
    }
  }
  if (j) { page_free_one((void *)a[j - 1]); }
}
void page_links(unsigned start, unsigned numbers) {
  page_links_pde(start, numbers, current_task()->pde);
}
void page_link(unsigned addr) {
  page_link_pde(addr, current_task()->pde);
}
void page_link_share(unsigned addr) {
  page_link_pde_share(addr, current_task()->pde);
}
void copy_from_phy_to_line(unsigned phy, unsigned line, unsigned pde, unsigned size) {
  unsigned pg = div_round_up(size, 0x1000);
  for (int i = 0; i < pg; i++) {
    memcpy((void *)page_get_phy_pde(line, pde), (void *)phy, size >= 0x1000 ? 0x1000 : size);
    size -= 0x1000;
    line += 0x1000;
    phy  += 0x1000;
  }
}
void set_line_address(unsigned val, unsigned line, unsigned pde, unsigned size) {
  unsigned pg = div_round_up(size, 0x1000);
  for (int i = 0; i < pg; i++) {
    memset((void *)page_get_phy_pde(line, pde), val, size >= 0x1000 ? 0x1000 : size);
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
void pf_set(u32 memsize) {
  u32 *pte = (u32 *)PTE_ADDRESS;
  for (int i = 0; pte != (u32 *)PAGE_END; pte++, i++) {
    if (i >= memsize / 4096 && i <= 0xc0000000 / 4096) { *pte = 0; }
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
void tpo2page(int *page, int t, int p) {
  *page = (t * 1024) + p;
}
void *page_malloc_one_no_mark() {
  int i;
  for (i = 0; i != 1024 * 1024; i++) {
    if (pages[i].count == 0) {
      int t, p;
      page2tpo(i, &t, &p);
      u32 addr         = get_line_address(t, p, 0);
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
    if (pages[i].count == 0) {
      int t, p;
      page2tpo(i, &t, &p);
      u32 addr         = get_line_address(t, p, 0);
      pages[i].task_id = get_tid(current_task());
      pages[i].count++;
      return (void *)addr;
    }
  }

  return NULL;
}
void *page_malloc_one_mark(unsigned tid) {
  int i;
  for (i = 0; i != 1024 * 1024; i++) {
    if (pages[i].count == 0) {
      int t, p;
      page2tpo(i, &t, &p);
      u32 addr         = get_line_address(t, p, 0);
      pages[i].task_id = tid;
      pages[i].count++;
      return (void *)addr;
    }
  }

  return NULL;
}
void *page_malloc_one_count_from_4gb() {
  int i = 0;
  for (i = IDX(memsize) - 1; i >= 0; i--) {
    if (pages[i].count == 0) {
      u32 addr         = PAGE(i);
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
    if (pages[i].count && pages[i].task_id == tid) {
      pages[i].count   = 0;
      pages[i].task_id = 0;
    }
  }
}
int get_pageinpte_address(int t, int p) {
  int page;
  tpo2page(&page, t, p);
  return (PTE_ADDRESS + page * 4);
}
void page_free_one(void *p) {
  if (IDX(p) >= 1024 * 1024) // 超过最大页
    return;
  if (pages[IDX(p)].count - 1 > 0) {
    if (pages[IDX(p)].task_id == current_task()->tid) { pages[IDX(p)].task_id = 0; }
    pages[IDX(p)].count--;
    return;
  }
  pages[IDX(p)].task_id = 0;
  pages[IDX(p)].count   = 0;
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
    if (pages[line].count == 0) {
      free++;
    } else {
      free = 0;
    }
    if (free == n) {
      for (int j = line - n + 1; j != line + 1; j++) {
        pages[j].task_id = 0;
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
  unsigned addr = get_line_address(t, p, 0);
  return (void *)addr;
}
void page_free(void *p, int size) {
  int n = ((size - 1) / (4 * 1024)) + 1;
  p     = (void *)((u32)p & 0xfffff000);
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
  target  = (void *)((int)target & 0xfffff000);
  start   = (void *)((int)start & 0xfffff000);
  end     = (void *)((int)end & 0xfffff000);
  u32 n   = (int)end - (int)start;
  n      /= 4 * 1024;
  n++;
  for (u32 i = 0; i < n; i++) {
    u32 tmp  = (u32)get_phy_address_for_line_address((void *)((u32)target + i * 4 * 1024));
    u32 tmp2 = (u32)get_phy_address_for_line_address((void *)((u32)start + i * 4 * 1024));
    set_phy_address_for_line_address((void *)((u32)target + i * 4 * 1024), (void *)tmp2);
    set_phy_address_for_line_address((void *)((u32)start + i * 4 * 1024), (void *)tmp);
  }
}
void change_page_task_id(int task_id, void *p, u32 size) {
  int page = get_page_from_line_address((int)p);
  for (int i = 0; i != ((size - 1) / (4 * 1024)) + 1; i++) {
    pages[page + i].task_id = task_id;
  }
}
void showPage() {
  // u32 *pte = (u32 *)PTE_ADDRESS;
  // printk("size = %d", sizeof(struct PTE_page_table));
  // for (int i = 0; pte != (u32 *)PAGE_END; pte++, i++) {
  //   printk("LINE ADDRESS: %08x PHY ADDRESS: %08x P=%d RW=%d US=%d USING=%d "
  //          "TASK=%d\n",
  //          i * 4096, (*pte >> 12) << 12, ((*pte) << 31) >> 31,
  //          ((*pte) << 30) >> 31, ((*pte) << 29) >> 31, pages[i].flag,
  //          pages[i].task_id);
  //*pte &= 0xffffffff-1;
  //}
}
u32 get_cr2() {
  unsigned r;
  asm volatile("mov %%cr2,%0" : "=r"(r));
  return r;
}
u32 page_get_attr_pde(unsigned vaddr, unsigned pde) {
  pde       += (unsigned)(DIDX(vaddr) * 4);
  void *pte  = (void *)(*(u32 *)pde & 0xfffff000);
  pte       += (unsigned)(TIDX(vaddr) * 4);
  return (*(u32 *)pte) & 0x00000fff;
}
u32 page_get_attr(unsigned vaddr) {
  return page_get_attr_pde(vaddr, current_task()->pde);
}

u32 page_get_phy_pde(unsigned vaddr, unsigned pde) {
  pde       += (unsigned)(DIDX(vaddr) * 4);
  void *pte  = (void *)(*(u32 *)pde & 0xfffff000);
  pte       += (unsigned)(TIDX(vaddr) * 4);
  return (*(u32 *)pte) & 0xfffff000;
}
u32 page_get_phy(unsigned vaddr) {
  return page_get_phy_pde(vaddr, current_task()->pde);
}
void copy_on_write(u32 vaddr) {
  void *pd      = (void *)current_task()->pde;                          // PDE页目录地址
  void *pde     = (void *)((unsigned)pd + (unsigned)(DIDX(vaddr) * 4)); // PTE地址
  void *pde_phy = (void *)(*(u32 *)(pde)&0xfffff000);                   // 页

  if (!(*(unsigned *)pde & PG_RWW) || !(*(unsigned *)pde & PG_USU)) { // PDE如果不可写
    // 不可写的话，就需要对PDE做COW操作
    unsigned backup = *(u32 *)(pde); // 用于备份原有页的属性
    if (pages[IDX(backup)].count < 2 || *(unsigned *)pde & PG_SHARED) {
      // 如果只有一个人引用，并且PDE属性是共享
      // 设置可写属性，然后进入下一步
      *(unsigned *)pde |= PG_RWW;
      *(unsigned *)pde |= PG_USU;
      *(unsigned *)pde |= PG_P;
      goto PDE_FLUSH;
    }
    // 进行COW
    *(u32 *)(pde) = (unsigned)page_malloc_one_count_from_4gb();      // 分配一页
    memcpy((void *)(*(u32 *)pde), pde_phy, 0x1000);                  // 复制内容
    *(u32 *)(pde) |= (backup & 0x00000fff) | PG_RWW | PG_USU | PG_P; // 设置属性（并且可读）
    pages[IDX(backup)].count--;                                      // 原有引用减少
  PDE_FLUSH:
    // 刷新快表
    flush_tlb(*(u32 *)(pde));
  } else {
  }
  void *pt  = (void *)(*(u32 *)pde & 0xfffff000);
  void *pte = pt + (unsigned)(TIDX(vaddr) * 4);
  if (!(*(unsigned *)pte & PG_RWW)) {
    if (pages[IDX(*(unsigned *)pte)].count < 2 || // 只有一个人引用
        *(unsigned *)pte & PG_SHARED /*或   这是一个SHARED页*/) {
      *(unsigned *)pte |= PG_RWW; // 设置RWW
      goto FLUSH;
    }
    // 获取旧页信息
    u32   old_pte = *(u32 *)pte;
    void *phy     = (void *)(old_pte & 0xfffff000);

    // 分配一个页
    //  logk("UPDATE %08x\n", vaddr);
    void *new_page = page_malloc_one_count_from_4gb();
    memcpy(new_page, phy, 0x1000);

    // 获取原先页的属性
    u32 attr = old_pte & 0x00000fff;

    // 设置PWU
    attr = attr | PG_RWW;

    // 计算新PTE
    u32 new_pte  = 0;
    new_pte     |= ((u32)new_page) & 0xfffff000;
    new_pte     |= attr;

    // 设置并更新
    pages[IDX(old_pte)].count--;
    *(u32 *)pte = new_pte;
  FLUSH:
    // 刷新TLB快表
    flush_tlb((unsigned)pte);
  }
  flush_tlb((unsigned)vaddr);
}
// 设置页属性和物理地址
void page_set_physics_attr(u32 vaddr, void *paddr, u32 attr) {
  unsigned pde_backup = current_task()->pde;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t             = DIDX(vaddr);
  p             = (vaddr >> 12) & 0x3ff;
  unsigned *pte = (u32 *)((pde_backup + t * 4));
  if (pages[IDX(*pte)].count > 1 && !(*pte & PG_SHARED)) { // 这里SHARED页就不进行COW操作
    pages[IDX(*pte)].count--;
    u32 old = *pte & 0xfffff000;
    *pte    = (unsigned)page_malloc_one_count_from_4gb();
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte |= 7;
  } else {
    *pte |= 7;
  }

  unsigned *physics = (unsigned *)((*pte & 0xfffff000) + p * 4);

  if (pages[IDX(*physics)].count > 1) { pages[IDX(*physics)].count--; }
  *physics  = (unsigned)paddr;
  *physics |= attr;
  flush_tlb((unsigned)pte);
  flush_tlb(vaddr);
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
}
void page_set_physics_attr_pde(u32 vaddr, void *paddr, u32 attr, unsigned pde_backup) {
  unsigned t, p;
  t             = DIDX(vaddr);
  p             = (vaddr >> 12) & 0x3ff;
  unsigned *pte = (u32 *)((pde_backup + t * 4));
  if (pages[IDX(*pte)].count > 1) { // 这里SHARED页就不进行COW操作
    pages[IDX(*pte)].count--;
    u32 old = *pte & 0xfffff000;
    *pte    = (unsigned)page_malloc_one_count_from_4gb();
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte |= 7;
  } else {
    *pte |= 7;
  }

  unsigned *physics = (unsigned *)((*pte & 0xfffff000) + p * 4);

  // if (pages[IDX(*physics)].count > 1) {
  //   pages[IDX(*physics)].count--;
  // }
  *physics  = (unsigned)paddr;
  *physics |= attr;
  flush_tlb((unsigned)pte);
  flush_tlb(vaddr);
}
extern struct TSS32 tss;
void PF(unsigned edi, unsigned esi, unsigned ebp, unsigned esp, unsigned ebx, unsigned edx,
        unsigned ecx, unsigned eax, unsigned gs, unsigned fs, unsigned es, unsigned ds,
        unsigned error, unsigned eip, unsigned cs, unsigned eflags) {
  unsigned pde = current_task()->pde;
  io_cli();
  set_cr3(PDE_ADDRESS); // 设置一个安全的页表
  void *line_address = (void *)get_cr2();
  if (!(page_get_attr((unsigned)line_address) & PG_P) ||     // 不存在
      (!(page_get_attr((unsigned)line_address) & PG_USU))) { // 用户不可写

    printk("Fatal error: Attempt to read/write a non-existent/kernel memory "
           "%08x at "
           "%08x. System "
           "halt \n   --- at PF()",
           line_address, eip);
    logk("Fatal error: Attempt to read/write a non-existent/kernel memory "
         "%08x at "
         "%08x. System "
         "halt \n   --- at PF()",
         line_address, eip);
    if (current_task()->user_mode) { // 用户级FAULT
      task_exit(-1);                 // 强制退出
    }
    io_cli();
    // 系统级FAULT
    asm volatile("hlt"); // 停机
    while (true)
      ;
  }
  copy_on_write((unsigned)line_address);
  set_cr3(pde);
  io_sti();
  return;
}

void page_set_attr(unsigned start, unsigned end, unsigned attr, unsigned pde) {
  int count = div_round_up(end - start, 0x1000); // 整除
  for (int i = 0; i < count; i++) {
    u32     *pde_entry  = (u32 *)(pde + DIDX(start + i * 0x1000) * 4);
    unsigned p          = *pde_entry & (0xfffff000);
    u32     *pte_entry  = (u32 *)(p + TIDX(start + i * 0x1000) * 4);
    *pte_entry         |= attr;
  }
  set_cr3(pde);
}