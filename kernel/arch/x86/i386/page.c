#include <arch/x86/i386/control.h>
#include <arch/x86/i386/memory.h>
#include <dos.h>
#include <irq.h>
#include <kasan.h>
#include <limits.h>
#include <page_fault.h>
#include <user_space.h>
#include <user_vm.h>
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
#define PAGE_BITMAP_ADDRESS (I386_PAGE_METADATA + PAGE_INFO_BYTES)
#define PAGE_ALLOCATOR_RESERVED_END (PAGE_BITMAP_ADDRESS + PAGE_BITMAP_BYTES - 1)
#define PAGE_USER_CLONE_BASE 0x70000000u
#define PAGE_KERNEL_BASE 0xc0000000u
#define PAGE_PDE_FREE_END 0xf1000000u
#define PG_P 1u
#define PG_RWW 2u
#define PG_USU 4u
#define PG_PCD 16u
#define PG_DEVICE 512u
#define PG_SHARED 1024u
#define PG_COW 2048u
#define PAGE_PRESENT_RW_FLAGS (PG_P | PG_RWW)
#define PAGE_USER_PRESENT_FLAGS (PG_P | PG_USU)
#define PAGE_USER_RW_FLAGS (PG_P | PG_USU | PG_RWW)

struct PAGE_INFO {
  uint32_t task_id;
  uint32_t count;
};

_Static_assert(PAGE_ALLOCATOR_RESERVED_END < KASAN_SHADOW_START,
               "page metadata overlaps KASAN shadow");

void *page_malloc_one_no_mark(void);
static struct PAGE_INFO *pages = (struct PAGE_INFO *)I386_PAGE_METADATA;
static uint32_t *page_free_bitmap = (uint32_t *)PAGE_BITMAP_ADDRESS;
static unsigned page_alloc_limit = PAGE_TOTAL_COUNT;
static unsigned page_low_hint = 0;
static unsigned page_high_hint = PAGE_TOTAL_COUNT - 1;
static unsigned page_run_hint = 0;

typedef struct {
  irq_state_t irq_state;
  arch_address_space_t active_address_space;
} page_table_access_t;

static page_table_access_t page_table_access_begin(void) {
  page_table_access_t access;
  access.irq_state = irq_save();
  access.active_address_space = arch_address_space_current();
  if (access.active_address_space != arch_address_space_kernel()) {
    arch_address_space_activate(arch_address_space_kernel());
  }
  return access;
}

static void page_table_access_end(const page_table_access_t *access) {
  if (access->active_address_space != arch_address_space_kernel()) {
    arch_address_space_activate(access->active_address_space);
  }
  irq_restore(access->irq_state);
}

static __attribute__((noreturn)) void page_ref_panic(const char *reason,
                                                     unsigned idx) {
  unsigned count = idx < PAGE_TOTAL_COUNT ? pages[idx].count : 0;
  unsigned owner = idx < PAGE_TOTAL_COUNT ? pages[idx].task_id : 0;
  Panic_K("page reference %s idx=%08x count=%u owner=%u", (char *)reason, idx,
          count, owner);
  (void)irq_save();
  for (;;) {
    asm volatile("hlt");
  }
}

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

static inline unsigned page_refcount_idx(unsigned idx) {
  if (idx >= PAGE_TOTAL_COUNT) {
    page_ref_panic("index out of range", idx);
  }
  return pages[idx].count;
}

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
    page_ref_panic("retain index out of range", idx);
  }
  if (pages[idx].count == UINT_MAX) {
    page_ref_panic("overflow", idx);
  }
  if (pages[idx].count == 0) {
    pages[idx].task_id = 0;
    page_bitmap_mark_used(idx);
    page_note_alloc_idx(idx);
  } else {
    pages[idx].task_id = 0;
  }
  pages[idx].count++;
}

static void page_ref_dec_idx(unsigned idx) {
  if (idx >= PAGE_TOTAL_COUNT) {
    page_ref_panic("release index out of range", idx);
  }
  if (pages[idx].count == 0) {
    page_ref_panic("underflow", idx);
  }
  if (pages[idx].count > 1 && pages[idx].task_id != 0) {
    page_ref_panic("shared page has owner", idx);
  }
  if (pages[idx].count == 1) {
    pages[idx].task_id = 0;
    pages[idx].count = 0;
    page_bitmap_mark_free(idx);
    page_note_free_idx(idx);
    return;
  }
  pages[idx].count--;
}

static inline void page_ref_inc_entry(uint32_t entry) {
  page_ref_inc_idx(IDX(page_entry_addr(entry)));
}

static inline void page_ref_dec_entry(uint32_t entry) {
  page_ref_dec_idx(IDX(page_entry_addr(entry)));
}

static void page_claim_idx(unsigned idx, uint32_t task_id) {
  page_ref_inc_idx(idx);
  if (pages[idx].count != 1) {
    page_ref_panic("claim reused page", idx);
  }
  pages[idx].task_id = task_id;
}

unsigned page_ref_count(uintptr_t paddr) {
  return page_refcount_idx(IDX(paddr));
}

void page_ref_release(uintptr_t paddr) { page_ref_dec_idx(IDX(paddr)); }

size_t page_used_count(uintptr_t physical_size) {
  unsigned limit = physical_size / PAGE_SIZE_BYTES;
  if (physical_size % PAGE_SIZE_BYTES) {
    limit++;
  }
  if (limit > PAGE_TOTAL_COUNT) {
    limit = PAGE_TOTAL_COUNT;
  }

  unsigned used = 0;
  for (unsigned idx = 0; idx < limit; idx++) {
    if (page_refcount_idx(idx) != 0) {
      used++;
    }
  }
  return used;
}

static void page_claim_range(unsigned start, unsigned count,
                             uint32_t task_id) {
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

static void *page_alloc_single(uint32_t task_id) {
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

static void *page_alloc_single_high(void) {
  int idx = page_find_free_high();
  if (idx < 0) {
    return NULL;
  }
  /* User mappings and page tables own explicit references. Task GC must not
   * reclaim them when one thread of a shared address space exits. */
  page_claim_idx((unsigned)idx, 0);
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
  for (uint32_t addr = I386_KERNEL_PAGE_TABLES,
                entry = page_entry_make(0, PAGE_PRESENT_RW_FLAGS);
       addr != I386_KERNEL_PAGE_TABLES_END; addr += PAGE_ENTRY_BYTES, entry += PAGE_SIZE_BYTES) {
    *(uint32_t *)(addr) = entry;
  }
  return;
}
static void init_page_manager(const boot_info_t *boot_info) {
  memset(pages, 0, PAGE_INFO_BYTES);
  memset(page_free_bitmap, 0, PAGE_BITMAP_BYTES);
  page_alloc_limit = PAGE_TOTAL_COUNT;
  page_low_hint = 0;
  page_high_hint = PAGE_TOTAL_COUNT - 1;
  page_run_hint = 0;

  if (boot_info == NULL || boot_info->memory_range_count == 0) {
    memset(page_free_bitmap, 0xff, PAGE_BITMAP_BYTES);
    return;
  }

  for (unsigned index = 0; index < PAGE_TOTAL_COUNT; index++) {
    pages[index].count = 1;
  }
  for (uint32_t index = 0; index < boot_info->memory_range_count; index++) {
    const boot_memory_range_t *range = &boot_info->memory_ranges[index];
    if (range->type != BOOT_MEMORY_USABLE || range->length == 0) {
      continue;
    }
    uint64_t end = range->base + range->length;
    if (end < range->base) {
      end = 0x100000000ull;
    }
    uint64_t start = (range->base + PAGE_SIZE_BYTES - 1) &
                     ~((uint64_t)PAGE_SIZE_BYTES - 1);
    end &= ~((uint64_t)PAGE_SIZE_BYTES - 1);
    if (start >= 0x100000000ull) {
      continue;
    }
    if (end > 0x100000000ull) {
      end = 0x100000000ull;
    }
    for (uint64_t address = start; address < end;
         address += PAGE_SIZE_BYTES) {
      unsigned page = (unsigned)(address / PAGE_SIZE_BYTES);
      pages[page].count = 0;
      page_bitmap_mark_free(page);
    }
  }
}

static void page_reserve_initial(uintptr_t start, uintptr_t end) {
  unsigned first = IDX(start);
  unsigned last = IDX(end);
  if (last >= PAGE_TOTAL_COUNT) {
    last = PAGE_TOTAL_COUNT - 1;
  }
  for (unsigned index = first; index <= last; index++) {
    if (pages[index].count == 0) {
      page_ref_inc_idx(index);
    }
  }
}

bool page_reserve_physical_range(uintptr_t start, uint32_t size) {
  if (size == 0 || start > UINT_MAX - (size - 1)) {
    return false;
  }

  unsigned first = IDX(start);
  unsigned last = IDX((start + size - 1));
  if (last >= PAGE_TOTAL_COUNT) {
    return false;
  }
  for (unsigned idx = first; idx <= last; idx++) {
    if (pages[idx].count != 0) {
      return false;
    }
  }
  for (unsigned idx = first; idx <= last; idx++) {
    page_ref_inc_idx(idx);
  }
  return true;
}
// 某些设计思路：
// 从0x70000000开始，到0xf0000000
// 大概2GB的内存可以给应用程序分配，OS使用前0x70000000的内存地址
// 为了防止应用程序和操作系统抢占前0x70000000的内存，所以page_link和copy_on_write是从后往前分配的
// OS应该是用不完0x70000000的，所以应用程序大概是可以用满2GB
arch_address_space_t
arch_address_space_clone(arch_address_space_t address_space) {
  arch_address_space_t result =
      (arch_address_space_t)(uintptr_t)page_malloc_one_no_mark();
  if (result == 0) {
    return 0;
  }

  page_table_access_t access = page_table_access_begin();
  for (int i = 0; i < 0x1000; i += 4) {
    unsigned int *pde_entry = (unsigned int *)(address_space + i);
    if (!page_entry_has_all(*pde_entry, PAGE_USER_PRESENT_FLAGS)) {
      continue;
    }
    unsigned p = page_entry_addr(*pde_entry);
    page_ref_inc_entry(*pde_entry);
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
      if (page_entry_has_any(*pte_entry, PG_RWW))
        *pte_entry = (*pte_entry & ~PG_RWW) | PG_COW;
    }
  }
  memcpy((void *)result, (void *)address_space, 0x1000);
  page_table_access_end(&access);

  return result;
}
void arch_address_space_retain(arch_address_space_t address_space) {
  if (address_space != arch_address_space_kernel()) {
    page_ref_inc_idx(IDX(address_space));
  }
}
void arch_address_space_release(arch_address_space_t address_space) {
  if (address_space == arch_address_space_kernel()) {
    return;
  }
  if (page_refcount_idx(IDX(address_space)) > 1) {
    page_ref_dec_idx(IDX(address_space));
    return;
  }
  page_table_access_t access = page_table_access_begin();
  if (access.active_address_space == address_space) {
    access.active_address_space = arch_address_space_kernel();
  }
  for (int i = 0; i < DIDX(PAGE_PDE_FREE_END) * 4; i += 4) {
    unsigned int *pde_entry = (unsigned int *)(address_space + i);
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
  page_free_one((void *)address_space);
  page_table_access_end(&access);
}

bool arch_address_space_prepare_exec(arch_address_space_t address_space) {
  bool result = true;
  page_table_access_t access = page_table_access_begin();

  for (uint32_t offset = DIDX(PAGE_USER_CLONE_BASE) * PAGE_ENTRY_BYTES;
       offset < PAGE_SIZE_BYTES; offset += PAGE_ENTRY_BYTES) {
    uint32_t *directory = (uint32_t *)(address_space + offset);
    if (!page_entry_has_all(*directory, PAGE_USER_PRESENT_FLAGS)) {
      continue;
    }

    unsigned references = page_refcount_entry(*directory);
    if (references == 0) {
      result = false;
      break;
    }
    if (references > 1) {
      uint32_t old_table = page_entry_addr(*directory);
      void *new_table = page_alloc_single_high();
      if (new_table == NULL) {
        result = false;
        break;
      }
      memcpy(new_table, (void *)old_table, PAGE_SIZE_BYTES);
      page_ref_dec_idx(IDX(old_table));
      *directory = page_entry_make((uintptr_t)new_table, PAGE_USER_RW_FLAGS);
    }

    *directory = page_entry_add_flags(*directory, PAGE_USER_RW_FLAGS);
    for (uint32_t index = 0; index < PAGE_ENTRY_COUNT; index++) {
      uint32_t *entry = page_table_entry_from_dir(*directory, index << 12);
      if (!page_entry_has_all(*entry, PAGE_USER_PRESENT_FLAGS)) {
        continue;
      }
      /* A new executable must not inherit any of its parent's user mappings. */
      page_ref_dec_entry(*entry);
      *entry = 0;
    }
    if (!result) {
      break;
    }
  }

  page_table_access_end(&access);
  return result;
}

static bool page_prepare_user_table(uint32_t *pde_entry) {
  if (!page_entry_has_all(*pde_entry, PAGE_USER_PRESENT_FLAGS)) {
    void *table = page_alloc_single_high();
    if (table == NULL) {
      return false;
    }
    memset(table, 0, PAGE_SIZE_BYTES);
    /* Kernel-only bootstrap tables are implicitly shared and are not reflected
     * in the page refcount.  A user mapping must detach instead of exposing or
     * modifying that global table. */
    *pde_entry = page_entry_make((unsigned)table, PAGE_USER_RW_FLAGS);
    return true;
  }
  if (page_refcount_entry(*pde_entry) <= 1) {
    *pde_entry = page_entry_add_flags(*pde_entry, PAGE_USER_RW_FLAGS);
    return true;
  }

  uint32_t old_entry = *pde_entry;
  void *new_table = page_alloc_single_high();
  if (new_table == NULL) {
    return false;
  }
  memcpy(new_table, (void *)page_entry_addr(old_entry), PAGE_SIZE_BYTES);
  page_ref_dec_entry(old_entry);
  *pde_entry = page_entry_make((uint32_t)(uintptr_t)new_table,
                               page_entry_flags(old_entry) |
                                   PAGE_USER_RW_FLAGS);
  return true;
}

bool arch_address_space_share(
    uintptr_t source, uintptr_t target, size_t size,
    arch_address_space_t source_address_space,
    arch_address_space_t target_address_space) {
  if (size == 0 || (source | target | size) & (PAGE_SIZE_BYTES - 1)) {
    return false;
  }

  uint32_t mapped = 0;
  bool result = false;
  page_table_access_t access = page_table_access_begin();

  for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE_BYTES) {
    uint32_t source_address = source + offset;
    uint32_t target_address = target + offset;
    uint32_t *source_directory =
        page_dir_entry(source_address_space, source_address);
    uint32_t source_directory_entry = *source_directory;
    if (!page_entry_has_all(source_directory_entry, PAGE_USER_PRESENT_FLAGS) ||
        !page_prepare_user_table(source_directory)) {
      goto rollback;
    }
    uint32_t *source_entry =
        page_table_entry_from_dir(*source_directory, source_address);
    uint32_t source_mapping = *source_entry;
    if (!page_entry_has_all(source_mapping, PAGE_USER_PRESENT_FLAGS)) {
      goto rollback;
    }

    uint32_t *target_directory =
        page_dir_entry(target_address_space, target_address);
    if (!page_prepare_user_table(target_directory)) {
      goto rollback;
    }
    uint32_t *target_entry =
        page_table_entry_from_dir(*target_directory, target_address);
    if (page_entry_has_any(*target_entry, PG_P)) {
      goto rollback;
    }

    *source_entry = page_entry_add_flags(source_mapping, PG_SHARED | PG_RWW);
    page_ref_inc_idx(IDX(page_entry_addr(source_mapping)));
    *target_entry = page_entry_make(page_entry_addr(source_mapping),
                                    PAGE_USER_RW_FLAGS | PG_SHARED);
    mapped += PAGE_SIZE_BYTES;
  }
  result = true;
  goto restore;

rollback:
  for (uint32_t offset = 0; offset < mapped; offset += PAGE_SIZE_BYTES) {
    uint32_t target_address = target + offset;
    uint32_t *target_directory =
        page_dir_entry(target_address_space, target_address);
    uint32_t *target_entry =
        page_table_entry_from_dir(*target_directory, target_address);
    page_ref_dec_entry(*target_entry);
    *target_entry = 0;
  }

restore:
  page_table_access_end(&access);
  return result;
}

bool arch_address_space_unmap_shared(
    uintptr_t target, size_t size,
    arch_address_space_t target_address_space) {
  if (size == 0 || (target | size) & (PAGE_SIZE_BYTES - 1)) {
    return false;
  }

  bool result = false;
  page_table_access_t access = page_table_access_begin();

  for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE_BYTES) {
    uint32_t *directory =
        page_dir_entry(target_address_space, target + offset);
    if (!page_entry_has_all(*directory, PAGE_USER_PRESENT_FLAGS)) {
      goto restore;
    }
    uint32_t *entry = page_table_entry_from_dir(*directory, target + offset);
    if (!page_entry_has_all(*entry, PAGE_USER_PRESENT_FLAGS) ||
        !page_entry_has_any(*entry, PG_SHARED)) {
      goto restore;
    }
  }

  for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE_BYTES) {
    uint32_t *directory =
        page_dir_entry(target_address_space, target + offset);
    if (!page_prepare_user_table(directory)) {
      goto restore;
    }
  }

  for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE_BYTES) {
    uint32_t *directory =
        page_dir_entry(target_address_space, target + offset);
    uint32_t *entry = page_table_entry_from_dir(*directory, target + offset);
    page_ref_dec_entry(*entry);
    *entry = 0;
  }
  result = true;

restore:
  page_table_access_end(&access);
  return result;
}

static int page_link_pde(uintptr_t addr, arch_address_space_t pde) {
  int result = 0;
  page_table_access_t access = page_table_access_begin();
  uint32_t *directory = page_dir_entry(pde, addr);

  if (!page_prepare_user_table(directory)) {
    goto restore;
  }

  uint32_t *entry = page_table_entry_from_dir(*directory, addr);
  void *new_page = page_alloc_single_high();
  if (new_page == NULL) {
    goto restore;
  }
  memset(new_page, 0, PAGE_SIZE_BYTES);
  if (page_entry_has_all(*entry, PAGE_USER_PRESENT_FLAGS)) {
    page_ref_dec_entry(*entry);
  }
  *entry = page_entry_make((uint32_t)(uintptr_t)new_page, PAGE_USER_RW_FLAGS);
  result = 1;
restore:
  page_table_access_end(&access);
  return result;
}
int page_link(uintptr_t addr) {
  return page_link_pde(addr, current_task()->address_space);
}

bool arch_user_map_zero(uintptr_t address) {
  static uintptr_t zero_page;
  if (address < USER_SPACE_START || address >= USER_HEAP_END ||
      (address & (PAGE_SIZE_BYTES - 1)))
    return false;
  bool result = false;
  page_table_access_t access = page_table_access_begin();
  uint32_t *directory = page_dir_entry(access.active_address_space, address);
  if (!page_prepare_user_table(directory))
    goto done;
  uint32_t *entry = page_table_entry_from_dir(*directory, address);
  if (*entry & PG_P)
    goto done;
  if (!zero_page) {
    void *page = page_alloc_single_high();
    if (!page)
      goto done;
    memset(page, 0, PAGE_SIZE_BYTES);
    zero_page = (uintptr_t)page;
  }
  page_ref_inc_idx(IDX(zero_page));
  *entry = page_entry_make(zero_page, PAGE_USER_PRESENT_FLAGS | PG_COW);
  result = true;
done:
  page_table_access_end(&access);
  return result;
}

unsigned arch_user_page_flags(uintptr_t address) {
  page_table_access_t access = page_table_access_begin();
  uint32_t directory = *page_dir_entry(access.active_address_space, address);
  unsigned flags = 0;
  if (page_entry_has_all(directory, PAGE_USER_PRESENT_FLAGS)) {
    uint32_t entry = *page_table_entry_from_dir(directory, address);
    if (page_entry_has_all(entry, PAGE_USER_PRESENT_FLAGS))
      flags = VM_READ | (entry & (PG_RWW | PG_COW) ? VM_WRITE : 0);
  }
  page_table_access_end(&access);
  return flags;
}

uintptr_t arch_user_find_free(uintptr_t lower, uintptr_t upper, size_t size) {
  page_table_access_t access = page_table_access_begin();
  size_t available = 0;
  uintptr_t result = 0;
  for (uintptr_t page = upper; page > lower;) {
    page -= PAGE_SIZE_BYTES;
    uint32_t directory = *page_dir_entry(access.active_address_space, page);
    bool occupied =
        page_entry_has_all(directory, PAGE_USER_PRESENT_FLAGS) &&
        page_entry_has_all(*page_table_entry_from_dir(directory, page),
                           PAGE_USER_PRESENT_FLAGS);
    available = occupied ? 0 : available + PAGE_SIZE_BYTES;
    if (available == size) {
      result = page;
      break;
    }
  }
  page_table_access_end(&access);
  return result;
}

static bool user_pages_change(uintptr_t address, size_t size,
                              unsigned protection, bool unmap) {
  bool result = false;
  page_table_access_t access = page_table_access_begin();
  for (size_t offset = 0; offset < size; offset += PAGE_SIZE_BYTES) {
    uint32_t *directory =
        page_dir_entry(access.active_address_space, address + offset);
    if (!page_entry_has_all(*directory, PAGE_USER_PRESENT_FLAGS))
      goto done;
    uint32_t entry = *page_table_entry_from_dir(*directory, address + offset);
    if (!page_entry_has_all(entry, PAGE_USER_PRESENT_FLAGS) ||
        (entry & PG_DEVICE) || (!unmap && (entry & PG_SHARED)) ||
        !page_prepare_user_table(directory))
      goto done;
  }
  for (size_t offset = 0; offset < size; offset += PAGE_SIZE_BYTES) {
    uint32_t directory =
        *page_dir_entry(access.active_address_space, address + offset);
    uint32_t *entry = page_table_entry_from_dir(directory, address + offset);
    if (unmap) {
      page_ref_dec_entry(*entry);
      *entry = 0;
    } else {
      *entry &= ~(PG_RWW | PG_COW);
      if (protection & VM_WRITE)
        *entry |= page_refcount_entry(*entry) > 1 ? PG_COW : PG_RWW;
    }
  }
  result = true;
done:
  page_table_access_end(&access);
  return result;
}

bool arch_user_protect(uintptr_t address, size_t size, unsigned protection) {
  /* Non-PAE i386 has no NX bit; read-only protection is still enforced. */
  return user_pages_change(address, size, protection, false);
}
bool arch_user_unmap(uintptr_t address, size_t size) {
  return user_pages_change(address, size, 0, true);
}
void *arch_module_allocate(size_t size) { return page_malloc(size); }
bool arch_module_protect(void *address, size_t size, bool writable,
                         bool executable) {
  (void)address;
  (void)size;
  (void)writable;
  (void)executable;
  /* The i386 backend uses its existing shared, executable kernel direct map. */
  return true;
}
void arch_module_free(void *address, size_t size) { page_free(address, size); }
void init_page(const boot_info_t *boot_info) {
  init_pdepte(I386_KERNEL_PAGE_DIRECTORY, I386_KERNEL_PAGE_TABLES, I386_KERNEL_PAGE_TABLES_END);
  init_page_manager(boot_info);
  page_reserve_initial(0, PAGE_ALLOCATOR_RESERVED_END);
  page_reserve_initial(KASAN_SHADOW_START, KASAN_SHADOW_END);
  page_reserve_initial(PAGE_KERNEL_BASE, 0xffffffffu);
  kasan_init();
  /* WP 必须与分页同时开启：ring0 写只读用户页也要触发 #PF 走 COW。 */
  arch_address_space_activate(arch_address_space_kernel());
  x86_cr0_write(x86_cr0_read() | X86_CR0_PG | X86_CR0_WP);
}

void *arch_mmio_map(uint64_t physical_address, size_t size) {
  if (size == 0 || physical_address > UINT_MAX ||
      size - 1 > UINT_MAX - (uintptr_t)physical_address) {
    return NULL;
  }
  uintptr_t first = (uintptr_t)physical_address & PAGE_ENTRY_ADDR_MASK;
  uintptr_t last = ((uintptr_t)physical_address + size - 1) & PAGE_ENTRY_ADDR_MASK;
  page_table_access_t access = page_table_access_begin();
  /* A process can replace a bootstrap PDE with its private user table.
   * Never hide those user mappings behind a physical identity alias. */
  for (unsigned index = DIDX(first); index <= DIDX(last); index++) {
    uint32_t kernel = ((uint32_t *)I386_KERNEL_PAGE_DIRECTORY)[index];
    uint32_t active = ((uint32_t *)access.active_address_space)[index];
    if (page_entry_addr(active) != page_entry_addr(kernel) ||
        page_entry_has_any(active, PG_USU)) {
      page_table_access_end(&access);
      return NULL;
    }
  }
  uint32_t *entries = (uint32_t *)I386_KERNEL_PAGE_TABLES;
  bool changed = false;
  for (unsigned index = IDX(first); index <= IDX(last); index++) {
    uint32_t flags = PAGE_PRESENT_RW_FLAGS | PG_PCD | PG_DEVICE;
    if (page_entry_addr(entries[index]) != PAGE(index) ||
        !page_entry_has_all(entries[index], flags)) {
      entries[index] = page_entry_make(PAGE(index), flags);
      changed = true;
    }
  }
  if (changed && access.active_address_space == arch_address_space_kernel()) {
    arch_address_space_activate(arch_address_space_kernel());
  }
  page_table_access_end(&access);
  return (void *)(uintptr_t)physical_address;
}

void pf_set(uintptr_t memsize) {
  uint32_t *pte = (uint32_t *)I386_KERNEL_PAGE_TABLES;
  for (int i = 0; pte != (uint32_t *)I386_KERNEL_PAGE_TABLES_END; pte++, i++) {
    if (!page_entry_has_any(*pte, PG_DEVICE) && i >= (int)(memsize / PAGE_SIZE_BYTES) &&
        i <= (int)(PAGE_KERNEL_BASE / PAGE_SIZE_BYTES)) {
      *pte = 0;
    }
  }
  page_alloc_limit = memsize / PAGE_SIZE_BYTES;
  if (page_alloc_limit < PAGE_TOTAL_COUNT && memsize < PAGE_KERNEL_BASE) {
    page_reserve_initial(memsize, PAGE_KERNEL_BASE - 1);
  }
  page_reserve_initial(KASAN_SHADOW_START, KASAN_SHADOW_END);
  if (page_alloc_limit == 0) {
    page_low_hint = 0;
    page_run_hint = 0;
    page_high_hint = 0;
  } else if (page_high_hint >= page_alloc_limit) {
    page_high_hint = page_alloc_limit - 1;
  }
}
void *page_malloc_one_no_mark() {
  return page_alloc_single(0);
}
void *page_malloc_one() {
  return page_alloc_single(get_tid(current_task()));
}
void gc(unsigned tid) {
  for (unsigned i = 0; i < PAGE_TOTAL_COUNT; i++) {
    unsigned count = page_refcount_idx(i);
    if ((count == 0 && pages[i].task_id != 0) ||
        (count > 1 && pages[i].task_id != 0)) {
      page_ref_panic("invalid owner", i);
    }
    if (tid != 0 && count == 1 && pages[i].task_id == tid) {
      page_ref_dec_idx(i);
    }
  }
}
static void page_free_one_internal(void *p, int kasan_track) {
  unsigned idx = IDX(p);
  if (idx >= PAGE_TOTAL_COUNT) // 超过最大页
    page_ref_panic("free index out of range", idx);
  unsigned count = page_refcount_idx(idx);
  if (count == 0) {
    page_ref_panic("free underflow", idx);
  }
  if (count > 1) {
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
void *page_malloc(int size) {
  if (size <= 0) {
    return NULL;
  }
  unsigned count = ((unsigned)size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
  int index = page_find_free_run(0, count);
  if (index < 0) {
    return NULL;
  }
  page_claim_range((unsigned)index, count, 0);
  page_run_hint = (unsigned)index + count < page_alloc_limit
                      ? (unsigned)index + count
                      : 0;
  uintptr_t address = PAGE((unsigned)index);
  size_t allocation_size = (size_t)count * PAGE_SIZE_BYTES;
  memset((void *)address, 0, allocation_size);
  kasan_page_alloc((void *)address, allocation_size, (uint32_t)size);
  return (void *)address;
}
void page_free(void *p, int size) {
  if (p == NULL || size <= 0) {
    return;
  }
  unsigned count = ((unsigned)size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
  uintptr_t address = (uintptr_t)p & ~(uintptr_t)(PAGE_SIZE_BYTES - 1);
  kasan_page_free((void *)address, (size_t)count * PAGE_SIZE_BYTES);
  for (unsigned i = 0; i < count; i++) {
    page_free_one_internal((void *)address, 0);
    address += PAGE_SIZE_BYTES;
  }
}
void change_page_task_id(uint32_t task_id, void *p, unsigned int size) {
  if (size == 0) {
    return;
  }
  unsigned page = IDX((uintptr_t)p);
  unsigned page_count = (size - 1) / PAGE_SIZE_BYTES + 1;
  for (unsigned i = 0; i < page_count; i++) {
    unsigned idx = (unsigned)page + i;
    unsigned refs = page_refcount_idx(idx);
    if (refs == 0) {
      page_ref_panic("assign owner to free page", idx);
    }
    if (refs > 1) {
      if (pages[idx].task_id != 0) {
        page_ref_panic("shared page has owner", idx);
      }
      continue;
    }
    pages[idx].task_id = task_id;
  }
}
page_fault_result_t arch_page_fault_resolve(uintptr_t address, uint32_t error) {
  if ((error != 0x3u && error != 0x7u) ||
      address < USER_SPACE_START || address > USER_HEAP_END) {
    return PAGE_FAULT_UNHANDLED;
  }

  arch_address_space_t active_address_space = arch_address_space_current();
  if (active_address_space < PAGE_SIZE_BYTES ||
      (active_address_space & (PAGE_SIZE_BYTES - 1)) != 0 ||
      page_refcount_idx(IDX(active_address_space)) == 0) {
    return PAGE_FAULT_UNHANDLED;
  }
  unsigned owner_tid = task_address_space_owner(active_address_space);
  if (owner_tid == (uint32_t)-1) {
    return PAGE_FAULT_UNHANDLED;
  }

  page_fault_result_t resolved = PAGE_FAULT_UNHANDLED;
  void *new_table = NULL;
  void *new_page = NULL;
  arch_address_space_activate(arch_address_space_kernel());

  uint32_t *pde = page_dir_entry(active_address_space, address);
  uint32_t old_pde = *pde;
  if (!page_entry_has_all(old_pde, PAGE_USER_PRESENT_FLAGS) ||
      page_entry_addr(old_pde) == 0 || page_refcount_entry(old_pde) == 0) {
    goto restore;
  }

  uint32_t *old_pte = page_table_entry_from_dir(old_pde, address);
  uint32_t old_pte_value = *old_pte;
  if (!page_entry_has_all(old_pte_value, PAGE_USER_PRESENT_FLAGS) ||
      page_entry_addr(old_pte_value) == 0 ||
      page_refcount_entry(old_pte_value) == 0) {
    goto restore;
  }

  bool pde_writable = page_entry_has_any(old_pde, PG_RWW);
  bool pte_writable = page_entry_has_any(old_pte_value, PG_RWW);
  if (pde_writable && pte_writable) {
    /* Another CPU may have completed COW before this CPU consumed its stale
     * read-only TLB entry.  Reloading CR3 below is sufficient once the current
     * page tables already describe a writable mapping. */
    resolved = PAGE_FAULT_RESOLVED;
    goto restore;
  }
  if (!(old_pte_value & PG_COW))
    goto restore;

  bool copy_table = page_refcount_entry(old_pde) > 1 &&
                    !page_entry_has_any(old_pde, PG_SHARED);
  bool copy_page = !pte_writable && page_refcount_entry(old_pte_value) > 1 &&
                   !page_entry_has_any(old_pte_value, PG_SHARED);
  if (copy_table) {
    new_table = page_alloc_single_high();
    if (new_table == NULL) {
      resolved = PAGE_FAULT_NO_MEMORY;
      goto restore;
    }
  }
  if (copy_page) {
    new_page = page_alloc_single_high();
    if (new_page == NULL) {
      resolved = PAGE_FAULT_NO_MEMORY;
      if (new_table != NULL) {
        page_free_one(new_table);
      }
      goto restore;
    }
  }

  uint32_t *target_pte = old_pte;
  if (new_table != NULL) {
    memcpy(new_table, (void *)page_entry_addr(old_pde), PAGE_SIZE_BYTES);
    target_pte = page_table_entry_from_dir((uint32_t)(uintptr_t)new_table,
                                           address);
  }
  if (new_page != NULL) {
    memcpy(new_page, (void *)page_entry_addr(old_pte_value), PAGE_SIZE_BYTES);
    *target_pte = page_entry_make(
        (uint32_t)(uintptr_t)new_page,
        page_entry_flags(old_pte_value) | PAGE_USER_RW_FLAGS);
  } else {
    *target_pte = page_entry_add_flags(old_pte_value, PAGE_USER_RW_FLAGS);
  }
  *target_pte &= ~PG_COW;

  if (new_table != NULL) {
    *pde = page_entry_make((uint32_t)(uintptr_t)new_table,
                           page_entry_flags(old_pde) | PAGE_USER_RW_FLAGS);
  } else {
    *pde = page_entry_add_flags(old_pde, PAGE_USER_RW_FLAGS);
  }
  if (new_page != NULL) {
    page_ref_dec_entry(old_pte_value);
  }
  if (new_table != NULL) {
    page_ref_dec_entry(old_pde);
  }
  resolved = PAGE_FAULT_RESOLVED;

restore:
  arch_address_space_activate(active_address_space);
  return resolved;
}
// 设置页属性和物理地址
static bool page_set_physics_attr(uint32_t vaddr, uintptr_t paddr,
                                  uint32_t attr) {
  unsigned pde = current_task()->address_space;
  page_table_access_t access = page_table_access_begin();
  uint32_t *directory = page_dir_entry(pde, vaddr);
  if (!page_prepare_user_table(directory)) {
    page_table_access_end(&access);
    return false;
  }

  uint32_t *physics = page_table_entry_from_dir(*directory, vaddr);

  uint32_t old_mapping = *physics;
  if (page_entry_has_all(old_mapping, PAGE_USER_PRESENT_FLAGS) &&
      page_entry_addr(old_mapping) != paddr) {
    page_ref_dec_entry(old_mapping);
  }
  if (!page_entry_has_all(old_mapping, PAGE_USER_PRESENT_FLAGS) ||
      page_entry_addr(old_mapping) != paddr)
    page_ref_inc_idx(IDX(paddr));
  *physics = page_entry_make(paddr, attr);
  page_table_access_end(&access);
  return true;
}

bool arch_address_space_map_user_device(uintptr_t user_address,
                                        uintptr_t physical_address,
                                        size_t size) {
  if (size == 0 || ((user_address | physical_address | size) &
                    (PAGE_SIZE_BYTES - 1)) != 0) {
    return false;
  }
  for (size_t offset = 0; offset < size; offset += PAGE_SIZE_BYTES) {
    if (!page_set_physics_attr(user_address + offset,
                               physical_address + offset,
                               PAGE_USER_RW_FLAGS | PG_SHARED)) {
      return false;
    }
  }
  return true;
}
