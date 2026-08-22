#include <dos.h>
#include <kasan.h>
#define IDX(addr) ((unsigned)addr >> 12)            // 获取 addr 的页索引
#define DIDX(addr) (((unsigned)addr >> 22) & 0x3ff) // 获取 addr 的页目录索引
#define TIDX(addr) (((unsigned)addr >> 12) & 0x3ff) // 获取 addr 的页表索引
#define PAGE(idx) ((unsigned)idx << 12) // 获取页索引 idx 对应的页开始的位置

#define PAGE_SIZE_BYTES 0x1000u
#define PAGE_ENTRY_COUNT 1024u
#define PAGE_TOTAL_COUNT (PAGE_ENTRY_COUNT * PAGE_ENTRY_COUNT)
#define PAGE_ENTRY_BYTES sizeof(uint32_t)
#define PAGE_ENTRY_ADDR_MASK 0xfffff000u
#define PAGE_ENTRY_FLAG_MASK 0x00000fffu
#define PAGE_ALLOC_BITMAP_WORD_BITS 32u
#define PAGE_INFO_BYTES (PAGE_TOTAL_COUNT * sizeof(struct PAGE_INFO))
#define PAGE_BITMAP_BYTES (PAGE_TOTAL_COUNT / 8u)
#define PAGE_BITMAP_WORDS (PAGE_TOTAL_COUNT / PAGE_ALLOC_BITMAP_WORD_BITS)
#define PAGE_BITMAP_ADDRESS (PAGE_MANNAGER + PAGE_INFO_BYTES)
#define PAGE_ALLOCATOR_RESERVED_END (PAGE_BITMAP_ADDRESS + PAGE_BITMAP_BYTES - 1)
#define PAGE_USER_CLONE_BASE 0x70000000u
#define PAGE_KERNEL_BASE 0xc0000000u
#define PAGE_PDE_FREE_END 0xf1000000u
#define PAGE_PRESENT_RW_FLAGS (PG_P | PG_RWW)
#define PAGE_USER_PRESENT_FLAGS (PG_P | PG_USU)
#define PAGE_USER_RW_FLAGS (PG_P | PG_USU | PG_RWW)

void *page_malloc_one_no_mark();
void flush_tlb(unsigned vaddr); 
unsigned div_round_up(unsigned num, unsigned size);
struct PAGE_INFO *pages = (struct PAGE_INFO *)PAGE_MANNAGER;
static uint32_t *page_free_bitmap = (uint32_t *)PAGE_BITMAP_ADDRESS;
static unsigned page_alloc_limit = PAGE_TOTAL_COUNT;
static unsigned page_low_hint = 0;
static unsigned page_high_hint = PAGE_TOTAL_COUNT - 1;
static unsigned page_run_hint = 0;

static inline unsigned page_entry_addr(uint32_t entry) {
  return entry & PAGE_ENTRY_ADDR_MASK;
}

static inline uint32_t page_entry_flags(uint32_t entry) {
  return entry & PAGE_ENTRY_FLAG_MASK;
}

static inline uint32_t page_entry_make(unsigned addr, uint32_t flags) {
  return (addr & PAGE_ENTRY_ADDR_MASK) | (flags & PAGE_ENTRY_FLAG_MASK);
}

static inline uint32_t page_entry_add_flags(uint32_t entry, uint32_t flags) {
  return page_entry_make(page_entry_addr(entry), page_entry_flags(entry) | flags);
}

static inline uint32_t page_entry_clear_flags(uint32_t entry, uint32_t flags) {
  return page_entry_make(page_entry_addr(entry),
                         page_entry_flags(entry) & ~flags);
}

static inline bool page_entry_has_all(uint32_t entry, uint32_t flags) {
  return (entry & flags) == flags;
}

static inline bool page_entry_has_any(uint32_t entry, uint32_t flags) {
  return (entry & flags) != 0;
}

static inline uint32_t *page_dir_entry(unsigned pde, unsigned vaddr) {
  return (uint32_t *)(pde + DIDX(vaddr) * PAGE_ENTRY_BYTES);
}

static inline uint32_t *page_table_entry_from_dir(uint32_t pde_entry,
                                                  unsigned vaddr) {
  return (uint32_t *)(page_entry_addr(pde_entry) +
                      TIDX(vaddr) * PAGE_ENTRY_BYTES);
}

static inline unsigned page_alloc_word(unsigned idx) { return idx >> 5; }

static inline uint32_t page_alloc_mask(unsigned idx) {
  return 1u << (idx & (PAGE_ALLOC_BITMAP_WORD_BITS - 1));
}

static inline void page_bitmap_mark_free(unsigned idx) {
  page_free_bitmap[page_alloc_word(idx)] |= page_alloc_mask(idx);
}

static inline void page_bitmap_mark_used(unsigned idx) {
  page_free_bitmap[page_alloc_word(idx)] &= ~page_alloc_mask(idx);
}

static inline bool page_bitmap_is_free(unsigned idx) {
  return (page_free_bitmap[page_alloc_word(idx)] & page_alloc_mask(idx)) != 0;
}

static inline unsigned page_refcount_idx(unsigned idx) { return pages[idx].count; }

static inline unsigned page_refcount_entry(uint32_t entry) {
  return page_refcount_idx(IDX(page_entry_addr(entry)));
}

static void page_note_free_idx(unsigned idx) {
  if (idx < page_low_hint) {
    page_low_hint = idx;
  }
  if (idx < page_run_hint) {
    page_run_hint = idx;
  }
  if (idx < page_alloc_limit && idx > page_high_hint) {
    page_high_hint = idx;
  }
}

static void page_note_alloc_idx(unsigned idx) {
  if (idx == page_low_hint) {
    page_low_hint = idx + 1 < page_alloc_limit ? idx + 1 : 0;
  }
  if (idx == page_run_hint) {
    page_run_hint = idx + 1 < page_alloc_limit ? idx + 1 : 0;
  }
  if (idx == page_high_hint) {
    page_high_hint = idx ? idx - 1 : 0;
  }
}

static void page_ref_inc_idx(unsigned idx) {
  if (idx >= PAGE_TOTAL_COUNT) {
    return;
  }
  if (pages[idx].count == 0) {
    page_bitmap_mark_used(idx);
    page_note_alloc_idx(idx);
  }
  pages[idx].count++;
}

static void page_ref_dec_idx(unsigned idx) {
  if (idx >= PAGE_TOTAL_COUNT || pages[idx].count == 0) {
    return;
  }
  pages[idx].count--;
  if (pages[idx].count == 0) {
    pages[idx].task_id = 0;
    page_bitmap_mark_free(idx);
    page_note_free_idx(idx);
  }
}

static inline void page_ref_inc_entry(uint32_t entry) {
  page_ref_inc_idx(IDX(page_entry_addr(entry)));
}

static inline void page_ref_dec_entry(uint32_t entry) {
  page_ref_dec_idx(IDX(page_entry_addr(entry)));
}

static void page_claim_idx(unsigned idx, uint8_t task_id) {
  page_ref_inc_idx(idx);
  pages[idx].task_id = task_id;
}

static void page_claim_range(unsigned start, unsigned count, uint8_t task_id) {
  for (unsigned idx = start; idx < start + count; idx++) {
    page_claim_idx(idx, task_id);
  }
}

static int page_find_free_low_from(unsigned start, unsigned limit) {
  if (start >= limit) {
    return -1;
  }

  unsigned first_word = start / PAGE_ALLOC_BITMAP_WORD_BITS;
  unsigned last_word = (limit - 1) / PAGE_ALLOC_BITMAP_WORD_BITS;
  uint32_t start_mask = ~0u << (start & (PAGE_ALLOC_BITMAP_WORD_BITS - 1));

  for (unsigned word = first_word; word <= last_word; word++) {
    uint32_t bits = page_free_bitmap[word];
    if (word == first_word) {
      bits &= start_mask;
    }
    if (word == last_word && (limit & (PAGE_ALLOC_BITMAP_WORD_BITS - 1))) {
      bits &= (1u << (limit & (PAGE_ALLOC_BITMAP_WORD_BITS - 1))) - 1u;
    }
    if (!bits) {
      continue;
    }
    return (int)(word * PAGE_ALLOC_BITMAP_WORD_BITS + __builtin_ctz(bits));
  }

  return -1;
}

static int page_find_free_low(void) {
  int found = page_find_free_low_from(page_low_hint, page_alloc_limit);
  if (found < 0 && page_low_hint) {
    found = page_find_free_low_from(0, page_low_hint);
  }
  return found;
}

static int page_find_free_high(void) {
  if (page_alloc_limit == 0) {
    return -1;
  }

  unsigned start =
      page_high_hint < page_alloc_limit ? page_high_hint : page_alloc_limit - 1;
  unsigned word = start / PAGE_ALLOC_BITMAP_WORD_BITS;
  uint32_t end_mask =
      (start & (PAGE_ALLOC_BITMAP_WORD_BITS - 1)) == 31
          ? ~0u
          : ((1u << ((start & (PAGE_ALLOC_BITMAP_WORD_BITS - 1)) + 1)) - 1u);

  for (;;) {
    uint32_t bits = page_free_bitmap[word] & end_mask;
    if (bits) {
      return (int)(word * PAGE_ALLOC_BITMAP_WORD_BITS +
                   (31u - __builtin_clz(bits)));
    }
    if (word == 0) {
      break;
    }
    word--;
    end_mask = ~0u;
  }

  return -1;
}

static int page_find_free_run_from(unsigned start, unsigned count,
                                   unsigned limit) {
  unsigned run = 0;
  unsigned run_start = 0;
  unsigned idx = start;

  while (idx < limit) {
    unsigned word = idx / PAGE_ALLOC_BITMAP_WORD_BITS;
    uint32_t bits = page_free_bitmap[word];
    if (!bits) {
      run = 0;
      idx = (word + 1) * PAGE_ALLOC_BITMAP_WORD_BITS;
      continue;
    }

    unsigned bit = idx & (PAGE_ALLOC_BITMAP_WORD_BITS - 1);
    while (bit < PAGE_ALLOC_BITMAP_WORD_BITS && idx < limit) {
      if (bits & (1u << bit)) {
        if (run == 0) {
          run_start = idx;
        }
        run++;
        if (run == count) {
          return (int)run_start;
        }
      } else {
        run = 0;
      }
      idx++;
      bit++;
    }
  }

  return -1;
}

static int page_find_free_run(unsigned start, unsigned count) {
  unsigned begin = start ? start : page_run_hint;
  if (begin >= page_alloc_limit) {
    begin = 0;
  }

  int found = page_find_free_run_from(begin, count, page_alloc_limit);
  if (found < 0 && begin) {
    found = page_find_free_run_from(0, count, begin);
  }
  return found;
}

static void *page_alloc_single(uint8_t task_id) {
  int idx = page_find_free_low();
  if (idx < 0) {
    return NULL;
  }
  page_claim_idx((unsigned)idx, task_id);
  page_low_hint =
      (unsigned)idx + 1 < page_alloc_limit ? (unsigned)idx + 1 : 0;
  if (page_run_hint == (unsigned)idx) {
    page_run_hint = page_low_hint;
  }
  kasan_page_alloc((void *)PAGE((unsigned)idx), PAGE_SIZE_BYTES,
                   PAGE_SIZE_BYTES);
  return (void *)PAGE((unsigned)idx);
}

static void *page_alloc_single_high(uint8_t task_id) {
  int idx = page_find_free_high();
  if (idx < 0) {
    return NULL;
  }
  page_claim_idx((unsigned)idx, task_id);
  page_high_hint = idx > 0 ? (unsigned)idx - 1 : 0;
  kasan_page_alloc((void *)PAGE((unsigned)idx), PAGE_SIZE_BYTES,
                   PAGE_SIZE_BYTES);
  return (void *)PAGE((unsigned)idx);
}

void init_pdepte(unsigned int pde_addr, unsigned pte_addr, unsigned page_end) {

  memset((void *)pde_addr, 0, page_end - pde_addr);
  // 这是初始化PDE 页目录
  for (uint32_t addr = pde_addr,
                entry = page_entry_make(pte_addr, PAGE_PRESENT_RW_FLAGS);
       addr != pte_addr; addr += PAGE_ENTRY_BYTES, entry += PAGE_SIZE_BYTES) {
    *(uint32_t *)(addr) = entry;
  }
  // 这是初始化PTE 页表
  for (uint32_t addr = PTE_ADDRESS,
                entry = page_entry_make(0, PAGE_PRESENT_RW_FLAGS);
       addr != PAGE_END; addr += PAGE_ENTRY_BYTES, entry += PAGE_SIZE_BYTES) {
    *(uint32_t *)(addr) = entry;
  }
  return;
}
void init_page_manager(struct PAGE_INFO *pg) {
  memset(pg, 0, PAGE_INFO_BYTES);
  memset(page_free_bitmap, 0xff, PAGE_BITMAP_BYTES);
  page_alloc_limit = PAGE_TOTAL_COUNT;
  page_low_hint = 0;
  page_high_hint = PAGE_TOTAL_COUNT - 1;
  page_run_hint = 0;
} // 全部置为0就好
void page_set_alloced(struct PAGE_INFO *pg, unsigned int start,
                      unsigned int end) {
  (void)pg;
  for (unsigned i = IDX(start); i <= IDX(end); i++) {
    page_ref_inc_idx(i); // 设置占用，但是没有进程引用
  }
}
// 某些设计思路：
// 从0x70000000开始，到0xf0000000
// 大概2GB的内存可以给应用程序分配，OS使用前0x70000000的内存地址
// 为了防止应用程序和操作系统抢占前0x70000000的内存，所以page_link和copy_on_write是从后往前分配的
// OS应该是用不完0x70000000的，所以应用程序大概是可以用满2GB
unsigned pde_clone(unsigned addr) {

  for (int i = 0; i < 0x1000; i += 4) {
    unsigned int *pde_entry = (unsigned int *)(addr + i);
    if (!page_entry_has_all(*pde_entry, PAGE_USER_PRESENT_FLAGS)) {
      continue;
    }
    unsigned p = page_entry_addr(*pde_entry);
    page_ref_inc_entry(*pde_entry);
    *pde_entry = page_entry_clear_flags(*pde_entry, PG_RWW);
    for (int j = 0; j < 0x1000; j += 4) {
      unsigned int *pte_entry = (unsigned int *)(p + j);
      if (!page_entry_has_all(*pte_entry, PAGE_USER_PRESENT_FLAGS)) {
        continue;
      }
      page_ref_inc_entry(*pte_entry);
      if (page_entry_has_any(*pte_entry, PG_SHARED)) {
        *pte_entry = page_entry_add_flags(*pte_entry, PG_RWW);
        continue;
      }
      *pte_entry = page_entry_clear_flags(*pte_entry, PG_RWW);
    }
  }
  unsigned result = (unsigned)page_malloc_one_no_mark();
  memcpy((void *)result, (void *)addr, 0x1000);
  flush_tlb(result);
  flush_tlb(addr);
  set_cr3(addr);

  return result;
}
void pde_retain(unsigned addr) {
  if (addr != PDE_ADDRESS) {
    page_ref_inc_idx(IDX(addr));
  }
}
void pde_reset(unsigned addr) {
  for (int i = 0; i < 0x1000; i += 4) {
    unsigned int *pde_entry = (unsigned int *)(addr + i);
    if (!page_entry_has_all(*pde_entry, PAGE_USER_PRESENT_FLAGS)) {
      continue;
    }
    *pde_entry = page_entry_add_flags(*pde_entry, PG_RWW);
  }
}
// BUG
void free_pde(unsigned addr) {
  if (addr == PDE_ADDRESS)
    return;
  if (page_refcount_idx(IDX(addr)) > 1) {
    page_ref_dec_idx(IDX(addr));
    return;
  }
  for (int i = 0; i < DIDX(PAGE_PDE_FREE_END) * 4; i += 4) {
    unsigned int *pde_entry = (unsigned int *)(addr + i);
    unsigned p = page_entry_addr(*pde_entry);
    if (!page_entry_has_all(*pde_entry, PAGE_USER_PRESENT_FLAGS)) {
      continue;
    }
    for (int j = 0; j < 0x1000; j += 4) {
      unsigned int *pte_entry = (unsigned int *)(p + j);
      if (page_entry_has_all(*pte_entry, PAGE_USER_PRESENT_FLAGS)) {
        page_ref_dec_entry(*pte_entry);
      }
    }

    page_ref_dec_entry(*pde_entry);
  }
  flush_tlb(addr);
  page_free_one((void *)addr);
}
// 刷新虚拟地址 vaddr 的 块表 TLB
void flush_tlb(unsigned vaddr) {
  asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

static int page_ensure_user_table(uint32_t *pde_entry) {
  if (page_entry_has_any(*pde_entry, PG_P)) {
    return 1;
  }
  void *table = page_malloc_one_count_from_4gb();
  if (table == NULL) {
    return 0;
  }
  memset(table, 0, PAGE_SIZE_BYTES);
  *pde_entry = page_entry_make((unsigned)table, PAGE_USER_RW_FLAGS);
  return 1;
}

int page_link_pde(unsigned addr, unsigned pde) {

  unsigned pde_backup = current_task()->pde;
  int result = 0;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t = DIDX(addr);
  p = (addr >> 12) & 0x3ff;
  uint32_t *pte = (uint32_t *)((pde + t * 4));

  if (!page_ensure_user_table(pte)) {
    goto restore;
  }

  if (page_refcount_entry(*pte) > 1) {
    // 这个页目录还有人引用，所以需要复制
    void *new_table = page_malloc_one_count_from_4gb();
    if (new_table == NULL) {
      goto restore;
    }
    page_ref_dec_entry(*pte);
    uint32_t old = page_entry_addr(*pte);
    *pte = (unsigned)new_table;
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  } else {
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  }

  uint32_t *physics =
      (uint32_t *)(page_entry_addr(*pte) + p * PAGE_ENTRY_BYTES); // PTE页表
  void *new_page = page_malloc_one_count_from_4gb();
  if (new_page == NULL) {
    goto restore;
  }
  // COW
  if (page_entry_has_all(*physics, PAGE_USER_PRESENT_FLAGS)) {
    page_ref_dec_entry(*physics);
  }
  *physics = (unsigned)new_page;
  *physics = page_entry_add_flags(*physics, PAGE_USER_RW_FLAGS);
  flush_tlb((unsigned)pte);
  flush_tlb(addr);
  result = 1;
restore:
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
  return result;
}
int page_link_pde_share(unsigned addr, unsigned pde) {

  unsigned pde_backup = current_task()->pde;
  int result = 0;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t = DIDX(addr);
  p = (addr >> 12) & 0x3ff;
  uint32_t *pte = (uint32_t *)((pde + t * 4));

  if (!page_ensure_user_table(pte)) {
    goto restore;
  }

  if (page_refcount_entry(*pte) > 1 && !page_entry_has_any(*pte, PG_SHARED)) {
    // 这个页目录还有人引用，所以需要复制
    void *new_table = page_malloc_one_count_from_4gb();
    if (new_table == NULL) {
      goto restore;
    }
    page_ref_dec_entry(*pte);
    uint32_t old = page_entry_addr(*pte);
    *pte = (unsigned)new_table;
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  } else {
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  }

  uint32_t *physics =
      (uint32_t *)(page_entry_addr(*pte) + p * PAGE_ENTRY_BYTES); // PTE页表
  void *new_page = page_malloc_one_count_from_4gb();
  if (new_page == NULL) {
    goto restore;
  }
  // COW
  if (page_entry_has_all(*physics, PAGE_USER_PRESENT_FLAGS)) {
    page_ref_dec_entry(*physics);
  }
  int flag = 0;
  if (page_entry_has_any(*physics, PG_SHARED)) {
    logk("THIS\n");
    flag = 1;
  }
  *physics = (unsigned)new_page;
  *physics = page_entry_add_flags(*physics, PAGE_USER_RW_FLAGS);
  if (flag) {
    *physics = page_entry_add_flags(*physics, PG_SHARED);
  }
  flush_tlb((unsigned)pte);
  flush_tlb(addr);
  result = 1;
restore:
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
  return result;
}
void page_link_pde_paddr(unsigned addr, unsigned pde, unsigned *paddr1,
                         unsigned paddr2) {
  unsigned pde_backup = current_task()->pde;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t = DIDX(addr);
  p = (addr >> 12) & 0x3ff;
  uint32_t *pte = (uint32_t *)((pde + t * 4));
  // logk("*pte = %08x\n",*pte);
  if (page_refcount_entry(*pte) > 1 && !page_entry_has_any(*pte, PG_SHARED)) {
    int flag = page_entry_has_any(*pte, PG_SHARED);
    page_ref_dec_entry(*pte);
    uint32_t old = page_entry_addr(*pte);
    *pte = *paddr1;
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
    *paddr1 = 0;
    if (flag) {
      *pte = page_entry_add_flags(*pte, PG_SHARED);
    }
  } else {
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  }

  uint32_t *physics =
      (uint32_t *)(page_entry_addr(*pte) + p * PAGE_ENTRY_BYTES);
  if (page_refcount_entry(*physics) > 1) {
    page_ref_dec_entry(*physics);
  }
  int flag = 0;
  if (page_entry_has_any(*physics, PG_SHARED)) {
    flag = 1;
  }
  *physics = paddr2;
  *physics = page_entry_add_flags(*physics, PAGE_USER_RW_FLAGS);
  if (flag) {
    *physics = page_entry_add_flags(*physics, PG_SHARED);
  }
  flush_tlb((unsigned)pte);
  flush_tlb(addr);
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
}
void page_links_pde(unsigned start, unsigned numbers, unsigned pde) {
  int times = 0;
  unsigned a[2] = {0, 0};
  int j = 0;
  while (times < numbers) {
    while (j < 2) {
      void *page = page_malloc_one_count_from_4gb();
      if (!page) {
        if (j) {
          page_free_one((void *)a[j - 1]);
        }
        return;
      }
      a[j++] = (unsigned)page;
    }

    page_link_pde_paddr(start, pde, &(a[0]), a[1]);
    times++;
    start += PAGE_SIZE_BYTES;
    if (a[0] != 0) {
      a[1] = 0;
      j = 1;
    } else {
      j = 0;
    }
  }

  if (j) {
    page_free_one((void *)a[j - 1]);
  }
}
void page_links(unsigned start, unsigned numbers) {
  page_links_pde(start, numbers, current_task()->pde);
}
int page_link(unsigned addr) {
  return page_link_pde(addr, current_task()->pde);
}
int page_link_share(unsigned addr) {
  return page_link_pde_share(addr, current_task()->pde);
}
void copy_from_phy_to_line(unsigned phy, unsigned line, unsigned pde,
                           unsigned size) {
  unsigned pg = div_round_up(size, 0x1000);
  for (int i = 0; i < pg; i++) {
    memcpy((void *)page_get_phy_pde(line, pde), (void *)phy,
           size >= 0x1000 ? 0x1000 : size);
    size -= 0x1000;
    line += 0x1000;
    phy += 0x1000;
  }
}
void set_line_address(unsigned val, unsigned line, unsigned pde,
                      unsigned size) {
  unsigned pg = div_round_up(size, 0x1000);
  for (int i = 0; i < pg; i++) {
    memset((void *)page_get_phy_pde(line, pde), val,
           size >= 0x1000 ? 0x1000 : size);
    size -= 0x1000;
    line += 0x1000;
  }
}
void page_unlink(unsigned addr) {}
void C_init_page() {
  init_pdepte(PDE_ADDRESS, PTE_ADDRESS, PAGE_END);
  init_page_manager(pages);
  page_set_alloced(pages, 0, PAGE_ALLOCATOR_RESERVED_END);
  page_set_alloced(pages, KASAN_SHADOW_START, KASAN_SHADOW_END);
  page_set_alloced(pages, PAGE_KERNEL_BASE, 0xffffffff);
  kasan_init();
}
void pf_set(unsigned int memsize) {
  uint32_t *pte = (uint32_t *)PTE_ADDRESS;
  for (int i = 0; pte != (uint32_t *)PAGE_END; pte++, i++) {
    if (i >= (int)(memsize / PAGE_SIZE_BYTES) &&
        i <= (int)(PAGE_KERNEL_BASE / PAGE_SIZE_BYTES)) {
      *pte = 0;
    }
  }
  page_alloc_limit = memsize / PAGE_SIZE_BYTES;
  if (page_alloc_limit < PAGE_TOTAL_COUNT && memsize < PAGE_KERNEL_BASE) {
    page_set_alloced(pages, memsize, PAGE_KERNEL_BASE - 1);
  }
  page_set_alloced(pages, KASAN_SHADOW_START, KASAN_SHADOW_END);
  if (page_alloc_limit == 0) {
    page_low_hint = 0;
    page_run_hint = 0;
    page_high_hint = 0;
  } else if (page_high_hint >= page_alloc_limit) {
    page_high_hint = page_alloc_limit - 1;
  }
}
int get_line_address(int t, int p, int o) {
  // 获取线性地址
  //  t:页目录地址
  //  p:页表地址
  //  o:页内偏移地址
  return (int)(((uint32_t)t << 22) + ((uint32_t)p << 12) + (uint32_t)o);
}
int get_page_from_line_address(int line_address) {
  int t, p, page;
  uint32_t addr = (uint32_t)line_address;
  t = addr >> 22;
  p = (addr >> 12) & 0x3ff;
  tpo2page(&page, t, p);
  return page;
}
void page2tpo(int page, int *t, int *p) {
  *t = page / 1024;
  *p = page % 1024;
}
void tpo2page(int *page, int t, int p) { *page = (t * 1024) + p; }
void *page_malloc_one_no_mark() {
  return page_alloc_single(0);
}
void *page_malloc_one() {
  return page_alloc_single(get_tid(current_task()));
}
void *page_malloc_one_mark(unsigned tid) {
  return page_alloc_single((uint8_t)tid);
}
void *page_malloc_one_count_from_4gb() {
  return page_alloc_single_high(get_tid(current_task()));
}
void gc(unsigned tid) {
  for (unsigned i = 0; i < PAGE_TOTAL_COUNT; i++) {
    if (pages[i].count && pages[i].task_id == tid) {
      pages[i].task_id = 0;
      while (pages[i].count) {
        page_ref_dec_idx(i);
      }
    }
  }
}
int get_pageinpte_address(int t, int p) {
  int page;
  tpo2page(&page, t, p);
  return (PTE_ADDRESS + page * 4);
}
static void page_free_one_internal(void *p, int kasan_track) {
  unsigned idx = IDX(p);
  if (idx >= PAGE_TOTAL_COUNT) // 超过最大页
    return;
  if (pages[idx].count > 1) {
    if (pages[idx].task_id == current_task()->tid) {
      pages[idx].task_id = 0;
    }
    page_ref_dec_idx(idx);
    return;
  }
  if (kasan_track) {
    kasan_page_free((void *)PAGE(idx), PAGE_SIZE_BYTES);
  }
  page_ref_dec_idx(idx);
}
void page_free_one(void *p) {
  page_free_one_internal(p, 1);
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
  if (n <= 0) {
    return -1;
  }

  int start = page_find_free_run((unsigned)(line < 0 ? 0 : line), (unsigned)n);
  if (start < 0) {
    return -1;
  }

  page_claim_range((unsigned)start, (unsigned)n, 0);
  page_run_hint = (unsigned)start + (unsigned)n < page_alloc_limit
                      ? (unsigned)start + (unsigned)n
                      : 0;
  return start;
}
void *page_malloc(int size) {
  int trace = size >= 1024 * 1024;
  int n = ((size - 1) / (4 * 1024)) + 1;
  if (trace) {
    logk("page_malloc: request size=%08x pages=%d\n", size, n);
  }
  int i = find_kpage(0, n);
  if (i < 0) {
    if (trace) {
      logk("page_malloc: find_kpage failed size=%08x pages=%d\n", size, n);
    }
    return NULL;
  }
  if (trace) {
    logk("page_malloc: found start_page=%08x addr=%08x\n", i,
         get_line_address(i / 1024, i % 1024, 0));
  }
  int t, p;
  page2tpo(i, &t, &p);
  if (trace) {
    logk("page_malloc: clear start addr=%08x bytes=%08x\n",
         get_line_address(t, p, 0), n * 4 * 1024);
  }
  memset((void *)get_line_address(t, p, 0), 0, n * 4 * 1024);
  if (trace) {
    logk("page_malloc: clear done addr=%08x\n", get_line_address(t, p, 0));
  }
  unsigned addr = get_line_address(t, p, 0);
  if (trace) {
    logk("page_malloc: kasan_page_alloc start addr=%08x total=%08x req=%08x\n",
         addr, n * 4 * 1024, size);
  }
  kasan_page_alloc((void *)addr, n * 4 * 1024, (uint32_t)size);
  if (trace) {
    logk("page_malloc: kasan_page_alloc done addr=%08x\n", addr);
  }
  return (void *)addr;
}
void page_free(void *p, int size) {
  int n = ((size - 1) / (4 * 1024)) + 1;
  p = (void *)((unsigned int)p & 0xfffff000);
  kasan_page_free(p, n * 4 * 1024);
  for (int i = 0; i < n; i++) {
    page_free_one_internal((void *)p, 0);
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
  // uint32_t *pte = (uint32_t *)PTE_ADDRESS;
  // printk("size = %d", sizeof(struct PTE_page_table));
  // for (int i = 0; pte != (uint32_t *)PAGE_END; pte++, i++) {
  //   printk("LINE ADDRESS: %08x PHY ADDRESS: %08x P=%d RW=%d US=%d USING=%d "
  //          "TASK=%d\n",
  //          i * 4096, (*pte >> 12) << 12, ((*pte) << 31) >> 31,
  //          ((*pte) << 30) >> 31, ((*pte) << 29) >> 31, pages[i].flag,
  //          pages[i].task_id);
  //*pte &= 0xffffffff-1;
  //}
}
unsigned int get_cr2() {
  unsigned r;
  asm volatile("mov %%cr2,%0" : "=r"(r));
  return r;
}
uint32_t page_get_attr_pde(unsigned vaddr, unsigned pde) {
  uint32_t *pde_entry = page_dir_entry(pde, vaddr);
  uint32_t *pte_entry = page_table_entry_from_dir(*pde_entry, vaddr);
  return page_entry_flags(*pte_entry);
}
uint32_t page_get_attr(unsigned vaddr) {
  unsigned pde;
  memcpy(&pde, &current_task()->pde, sizeof(pde));
  return page_get_attr_pde(vaddr, pde);
}

uint32_t page_get_phy_pde(unsigned vaddr, unsigned pde) {
  uint32_t *pde_entry = page_dir_entry(pde, vaddr);
  uint32_t *pte_entry = page_table_entry_from_dir(*pde_entry, vaddr);
  return page_entry_addr(*pte_entry);
}
uint32_t page_get_phy(unsigned vaddr) {
  return page_get_phy_pde(vaddr, current_task()->pde);
}
void copy_on_write(uint32_t vaddr) {
  void *pd = (void *)current_task()->pde; // PDE页目录地址
  uint32_t *pde = page_dir_entry((unsigned)pd, vaddr);              // PTE地址
  void *pde_phy = (void *)page_entry_addr(*pde);                    // 页

  if (!page_entry_has_any(*pde, PG_RWW) || !page_entry_has_any(*pde, PG_USU)) {
    // PDE如果不可写
    // 不可写的话，就需要对PDE做COW操作
    unsigned backup = *pde; // 用于备份原有页的属性
    if (page_refcount_entry(backup) < 2 || page_entry_has_any(*pde, PG_SHARED)) {
      // 如果只有一个人引用，并且PDE属性是共享
      // 设置可写属性，然后进入下一步
      *pde = page_entry_add_flags(*pde, PAGE_USER_RW_FLAGS);
      goto PDE_FLUSH;
    }
    // 进行COW
    *pde = (unsigned)page_malloc_one_count_from_4gb(); // 分配一页
    memcpy((void *)(*pde), pde_phy, 0x1000);           // 复制内容
    *pde = page_entry_add_flags(*pde,
                                page_entry_flags(backup) | PAGE_USER_RW_FLAGS);
    page_ref_dec_entry(backup); // 原有引用减少
  PDE_FLUSH:
    // 刷新快表
    flush_tlb(*pde);
  } else {
  }
  uint32_t *pte = page_table_entry_from_dir(*pde, vaddr);
  if (!page_entry_has_any(*pte, PG_RWW)) {
    if (page_refcount_entry(*pte) < 2 || // 只有一个人引用
        page_entry_has_any(*pte, PG_SHARED) /*或   这是一个SHARED页*/) {
      *pte = page_entry_add_flags(*pte, PG_RWW); // 设置RWW
      goto FLUSH;
    }
    // 获取旧页信息
    unsigned int old_pte = *pte;
    void *phy = (void *)page_entry_addr(old_pte);

    // 分配一个页
    //  logk("UPDATE %08x\n", vaddr);
    void *new_page = page_malloc_one_count_from_4gb();
    memcpy(new_page, phy, 0x1000);

    // 获取原先页的属性
    unsigned int attr = old_pte & 0x00000fff;

    // 设置PWU
    attr = attr | PG_RWW;

    // 计算新PTE
    unsigned int new_pte = page_entry_make((unsigned int)new_page, attr);

    // 设置并更新
    page_ref_dec_entry(old_pte);
    *pte = new_pte;
  FLUSH:
    // 刷新TLB快表
    flush_tlb((unsigned)pte);
  }
  flush_tlb((unsigned)vaddr);
}
// 设置页属性和物理地址
void page_set_physics_attr(uint32_t vaddr, void *paddr, uint32_t attr) {
  unsigned pde_backup = current_task()->pde;
  current_task()->pde = PDE_ADDRESS;
  set_cr3(PDE_ADDRESS);
  unsigned t, p;
  t = DIDX(vaddr);
  p = (vaddr >> 12) & 0x3ff;
  uint32_t *pte = (uint32_t *)((pde_backup + t * 4));
  if (page_refcount_entry(*pte) > 1 &&
      !page_entry_has_any(*pte, PG_SHARED)) { // 这里SHARED页就不进行COW操作
    page_ref_dec_entry(*pte);
    uint32_t old = page_entry_addr(*pte);
    *pte = (unsigned)page_malloc_one_count_from_4gb();
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  } else {
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  }

  uint32_t *physics =
      (uint32_t *)(page_entry_addr(*pte) + p * PAGE_ENTRY_BYTES);

  uint32_t old_mapping = *physics;
  if (page_entry_has_all(old_mapping, PAGE_USER_PRESENT_FLAGS) &&
      page_entry_addr(old_mapping) != (unsigned)paddr) {
    page_ref_dec_entry(old_mapping);
  }
  if (!page_entry_has_all(old_mapping, PAGE_USER_PRESENT_FLAGS) ||
      page_entry_addr(old_mapping) != (unsigned)paddr)
    page_ref_inc_idx(IDX(paddr));
  *physics = page_entry_make((unsigned)paddr, attr);
  flush_tlb((unsigned)pte);
  flush_tlb(vaddr);
  current_task()->pde = pde_backup;
  set_cr3(pde_backup);
}
void page_set_physics_attr_pde(uint32_t vaddr, void *paddr, uint32_t attr,
                               unsigned pde_backup) {
  unsigned t, p;
  t = DIDX(vaddr);
  p = (vaddr >> 12) & 0x3ff;
  uint32_t *pte = (uint32_t *)((pde_backup + t * 4));
  if (page_refcount_entry(*pte) > 1) { // 这里SHARED页就不进行COW操作
    page_ref_dec_entry(*pte);
    uint32_t old = page_entry_addr(*pte);
    *pte = (unsigned)page_malloc_one_count_from_4gb();
    memcpy((void *)(*pte), (void *)old, 0x1000);
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  } else {
    *pte = page_entry_add_flags(*pte, PAGE_USER_RW_FLAGS);
  }

  uint32_t *physics =
      (uint32_t *)(page_entry_addr(*pte) + p * PAGE_ENTRY_BYTES);

  uint32_t old_mapping = *physics;
  if (page_entry_has_all(old_mapping, PAGE_USER_PRESENT_FLAGS) &&
      page_entry_addr(old_mapping) != (unsigned)paddr) {
    page_ref_dec_entry(old_mapping);
  }
  if (!page_entry_has_all(old_mapping, PAGE_USER_PRESENT_FLAGS) ||
      page_entry_addr(old_mapping) != (unsigned)paddr)
    page_ref_inc_idx(IDX(paddr));
  *physics = page_entry_make((unsigned)paddr, attr);
  flush_tlb((unsigned)pte);
  flush_tlb(vaddr);
}
extern struct TSS32 tss;
void PF(unsigned edi, unsigned esi, unsigned ebp, unsigned esp, unsigned ebx,
        unsigned edx, unsigned ecx, unsigned eax, unsigned gs, unsigned fs,
        unsigned es, unsigned ds, unsigned error, unsigned eip, unsigned cs,
        unsigned eflags) {
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
    for (;;)
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
    unsigned vaddr = start + i * PAGE_SIZE_BYTES;
    uint32_t *pde_entry = page_dir_entry(pde, vaddr);
    uint32_t *pte_entry = page_table_entry_from_dir(*pde_entry, vaddr);
    *pte_entry = page_entry_add_flags(*pte_entry, attr);
  }
  set_cr3(pde);
}
