#include <arch/x86/x86_64/cpu.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <page_fault.h>
#include <user_space.h>
#include <user_vm.h>

enum { PAGE_BYTES = 4096, TABLE_ENTRIES = 512 };
#define PTE_ADDRESS 0x000ffffffffff000ull
#define PTE_PRESENT 1ull
#define PTE_WRITE 2ull
#define PTE_USER 4ull
#define PTE_UNCACHED 16ull
#define PTE_LARGE 128ull
#define PTE_COW 512ull
#define PTE_SHARED 1024ull
#define PTE_DEVICE 2048ull
#define PTE_NX (1ull << 63)

typedef struct {
  uint32_t references, owner;
} physical_page_t;
static physical_page_t *pages;
static size_t page_count, allocation_hint, user_hint;
static uint64_t zero_page;
arch_address_space_t x64_kernel_cr3;

static __attribute__((noreturn)) void page_panic(void) {
  Panic_K("invalid physical page reference");
  arch_halt();
}

static void page_retain(uint64_t physical) {
  size_t index = physical / PAGE_BYTES;
  if (index >= page_count || !pages[index].references ||
      pages[index].references >= UINT_MAX - 1) {
    page_panic();
  }
  pages[index].references++;
  pages[index].owner = 0;
}

static void page_release(uint64_t physical) {
  size_t index = physical / PAGE_BYTES;
  if (index >= page_count || !pages[index].references ||
      pages[index].references == UINT_MAX) {
    page_panic();
  }
  if (!--pages[index].references) {
    pages[index].owner = 0;
    if (index < allocation_hint)
      allocation_hint = index;
    if (index >= user_hint)
      user_hint = index + 1;
  }
}

void *page_malloc(int size) {
  if (size <= 0)
    return NULL;
  size_t count = ((size_t)size + PAGE_BYTES - 1) / PAGE_BYTES;
  irq_state_t state = irq_save();
  size_t begin = allocation_hint;
  for (unsigned pass = 0; pass < 2; pass++) {
    size_t run = 0;
    size_t limit = pass ? begin : page_count;
    for (size_t i = pass ? 0 : begin; i < limit; i++) {
      run = pages[i].references ? 0 : run + 1;
      if (run != count)
        continue;
      size_t first = i + 1 - count;
      for (size_t j = first; j <= i; j++)
        pages[j] = (physical_page_t){1, 0};
      allocation_hint = i + 1 < page_count ? i + 1 : 0;
      void *result = x64_physical_pointer(first * PAGE_BYTES);
      memset(result, 0, count * PAGE_BYTES);
      irq_restore(state);
      return result;
    }
  }
  irq_restore(state);
  return NULL;
}

void *page_malloc_one_no_mark(void) { return page_malloc(PAGE_BYTES); }
/* Keep lower physical memory available for devices with 32-bit DMA windows. */
static void *user_page_allocate(void) {
  irq_state_t state = irq_save();
  while (user_hint) {
    size_t index = --user_hint;
    if (pages[index].references)
      continue;
    pages[index] = (physical_page_t){1, 0};
    void *page = x64_physical_pointer(index * PAGE_BYTES);
    memset(page, 0, PAGE_BYTES);
    irq_restore(state);
    return page;
  }
  irq_restore(state);
  return NULL;
}
void *page_malloc_one(void) {
  void *result = page_malloc(PAGE_BYTES);
  if (result)
    change_page_task_id(current_task()->tid, result, PAGE_BYTES);
  return result;
}
void page_free(void *pointer, int size) {
  if (!pointer || size <= 0)
    return;
  irq_state_t state = irq_save();
  uint64_t physical = x64_virtual_physical(pointer);
  size_t count = ((size_t)size + PAGE_BYTES - 1) / PAGE_BYTES;
  for (size_t i = 0; i < count; i++)
    page_release(physical + i * PAGE_BYTES);
  irq_restore(state);
}
void page_free_one(void *pointer) { page_free(pointer, PAGE_BYTES); }

void change_page_task_id(uint32_t tid, void *pointer, unsigned size) {
  if (!size)
    return;
  size_t first = x64_virtual_physical(pointer) / PAGE_BYTES;
  size_t count = ((size_t)size + PAGE_BYTES - 1) / PAGE_BYTES;
  irq_state_t state = irq_save();
  if (first >= page_count || count > page_count - first)
    page_panic();
  for (size_t i = first; i < first + count; i++) {
    if (pages[i].references == 1)
      pages[i].owner = tid;
  }
  irq_restore(state);
}

void gc(unsigned tid) {
  if (!tid)
    return;
  for (size_t i = 0; i < page_count; i++) {
    if (pages[i].owner == tid)
      page_release(i * PAGE_BYTES);
  }
}

unsigned page_ref_count(uintptr_t physical) {
  return physical / PAGE_BYTES < page_count
             ? pages[physical / PAGE_BYTES].references
             : 0;
}
void page_ref_release(uintptr_t physical) { page_release(physical); }
size_t page_used_count(uintptr_t physical_size) {
  size_t count = physical_size / PAGE_BYTES;
  if (count > page_count)
    count = page_count;
  size_t used = 0;
  for (size_t i = 0; i < count; i++)
    used += pages[i].references != 0;
  return used;
}

bool page_reserve_physical_range(uintptr_t physical, uint32_t size) {
  if (!size || physical > UINT64_MAX - size)
    return false;
  size_t first = physical / PAGE_BYTES;
  size_t last = (physical + size - 1) / PAGE_BYTES;
  if (last >= page_count)
    return false;
  for (size_t i = first; i <= last; i++) {
    if (pages[i].references != UINT_MAX && pages[i].references != 0)
      return false;
  }
  for (size_t i = first; i <= last; i++)
    pages[i].references = UINT_MAX;
  return true;
}

/* Walk any address space through the direct map; never switch CR3 to edit it.
 */
static uint64_t *page_entry(arch_address_space_t root, uintptr_t address,
                            bool create) {
  uint64_t *table = x64_physical_pointer(root);
  for (int shift = 39; shift > 12; shift -= 9) {
    uint64_t *entry = &table[(address >> shift) & 511];
    if (!(*entry & PTE_PRESENT)) {
      if (!create)
        return NULL;
      void *next = page_malloc_one_no_mark();
      if (!next)
        return NULL;
      *entry = x64_virtual_physical(next) | PTE_PRESENT | PTE_WRITE |
               (address < USER_SPACE_END ? PTE_USER : 0);
    }
    if (*entry & PTE_LARGE)
      return NULL;
    table = x64_physical_pointer(*entry & PTE_ADDRESS);
  }
  return &table[(address >> 12) & 511];
}

static uint64_t virtual_translate(const void *pointer, uint64_t *cache_flags) {
  uintptr_t address = (uintptr_t)pointer;
  uint64_t *table = x64_physical_pointer(arch_address_space_current());
  for (int shift = 39; shift >= 12; shift -= 9) {
    uint64_t entry = table[(address >> shift) & 511];
    if (!(entry & PTE_PRESENT))
      return UINT64_MAX;
    if (shift == 12 || (entry & PTE_LARGE)) {
      uint64_t mask = (1ull << shift) - 1;
      if (cache_flags) {
        *cache_flags = entry & (8 | PTE_UNCACHED);
        if (entry & (shift == 12 ? 128ull : 4096ull))
          *cache_flags |= 128;
      }
      return (entry & PTE_ADDRESS & ~mask) | (address & mask);
    }
    table = x64_physical_pointer(entry & PTE_ADDRESS);
  }
  return UINT64_MAX;
}
uint64_t x64_virtual_physical(const void *pointer) {
  return virtual_translate(pointer, NULL);
}

static void mapping_release(uint64_t entry) {
  if ((entry & PTE_PRESENT) && !(entry & PTE_DEVICE)) {
    page_release(entry & PTE_ADDRESS);
  }
}

static void table_destroy(uint64_t physical, unsigned level) {
  uint64_t *table = x64_physical_pointer(physical);
  for (size_t i = 0; i < TABLE_ENTRIES; i++) {
    if (!(table[i] & PTE_PRESENT))
      continue;
    if (level > 1)
      table_destroy(table[i] & PTE_ADDRESS, level - 1);
    else
      mapping_release(table[i]);
  }
  page_release(physical);
}

static uint64_t table_clone(uint64_t physical, unsigned level) {
  uint64_t *source = x64_physical_pointer(physical);
  uint64_t *target = page_malloc_one_no_mark();
  if (!target)
    return 0;
  for (size_t i = 0; i < TABLE_ENTRIES; i++) {
    uint64_t entry = source[i];
    if (!(entry & PTE_PRESENT))
      continue;
    if (level > 1) {
      uint64_t child = table_clone(entry & PTE_ADDRESS, level - 1);
      if (!child) {
        table_destroy(x64_virtual_physical(target), level);
        return 0;
      }
      target[i] = child | PTE_PRESENT | PTE_WRITE | PTE_USER;
    } else {
      if (!(entry & PTE_DEVICE))
        page_retain(entry & PTE_ADDRESS);
      if ((entry & PTE_WRITE) && !(entry & (PTE_SHARED | PTE_DEVICE))) {
        entry = (entry & ~PTE_WRITE) | PTE_COW;
        source[i] = entry;
      }
      target[i] = entry;
    }
  }
  return x64_virtual_physical(target);
}

arch_address_space_t arch_address_space_clone(arch_address_space_t source) {
  irq_state_t state = irq_save();
  uint64_t *original = x64_physical_pointer(source);
  uint64_t *root = page_malloc_one_no_mark();
  if (!root) {
    irq_restore(state);
    return 0;
  }
  memcpy(root + 256, original + 256, PAGE_BYTES / 2);
  uint64_t result = x64_virtual_physical(root);
  for (size_t i = 0; i < 256; i++) {
    if (!(original[i] & PTE_PRESENT))
      continue;
    uint64_t child = table_clone(original[i] & PTE_ADDRESS, 3);
    if (!child) {
      arch_address_space_release(result);
      result = 0;
      break;
    }
    root[i] = child | PTE_PRESENT | PTE_WRITE | PTE_USER;
  }
  /* The parent's writable translations must not survive the COW conversion. */
  x64_tlb_invalidate(source, 0, 0);
  irq_restore(state);
  return result;
}

void arch_address_space_retain(arch_address_space_t root) {
  if (root != x64_kernel_cr3)
    page_retain(root);
}
bool arch_address_space_prepare_exec(arch_address_space_t root) {
  if (root == x64_kernel_cr3)
    return false;
  uint64_t *table = x64_physical_pointer(root);
  for (size_t i = 0; i < 256; i++) {
    if (table[i] & PTE_PRESENT)
      table_destroy(table[i] & PTE_ADDRESS, 3);
    table[i] = 0;
  }
  x64_tlb_invalidate(root, 0, 0);
  return true;
}
void arch_address_space_release(arch_address_space_t root) {
  if (!root || root == x64_kernel_cr3)
    return;
  if (pages[root / PAGE_BYTES].references == 1) {
    if (root == arch_address_space_current())
      arch_address_space_activate(x64_kernel_cr3);
    arch_address_space_prepare_exec(root);
  }
  page_release(root);
}

int page_link(uintptr_t address) {
  if (address < USER_SPACE_START || address > USER_HEAP_END || (address & 4095))
    return 0;
  arch_address_space_t root = arch_address_space_current();
  uint64_t *entry = page_entry(root, address, true);
  if (!entry)
    return 0;
  void *page = user_page_allocate();
  if (!page)
    return 0;
  uint64_t previous = *entry;
  mapping_release(previous);
  *entry =
      x64_virtual_physical(page) | PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_NX;
  if (previous & PTE_PRESENT)
    x64_tlb_invalidate(root, address, PAGE_BYTES);
  return 1;
}

bool arch_user_map_zero(uintptr_t address) {
  if (address < USER_SPACE_START || address >= USER_HEAP_END ||
      (address & 4095))
    return false;
  uint64_t *entry = page_entry(arch_address_space_current(), address, true);
  if (!entry || (*entry & PTE_PRESENT))
    return false;
  if (!zero_page) {
    void *page = user_page_allocate();
    if (!page)
      return false;
    zero_page = x64_virtual_physical(page);
  }
  // Keep one permanent reference so the shared zero page is never writable.
  page_retain(zero_page);
  *entry = zero_page | PTE_PRESENT | PTE_USER | PTE_COW | PTE_NX;
  return true;
}

bool x64_user_access(uintptr_t address, size_t size, bool writable) {
  if (address < USER_SPACE_START || address >= USER_SPACE_END ||
      size > USER_SPACE_END - address)
    return false;
  if (!size)
    return true;
  uintptr_t last = (address + size - 1) & ~(uintptr_t)4095;
  for (uintptr_t p = address & ~(uintptr_t)4095;; p += PAGE_BYTES) {
    uint64_t *entry = page_entry(arch_address_space_current(), p, false);
    if (!entry ||
        (*entry & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER) ||
        (writable && !(*entry & (PTE_WRITE | PTE_COW))))
      return false;
    if (p == last)
      return true;
  }
}

bool x64_user_protect(uintptr_t address, size_t size, bool writable,
                      bool executable) {
  return arch_user_protect(address, size,
                           VM_READ | (writable ? VM_WRITE : 0) |
                               (executable ? VM_EXEC : 0));
}

unsigned arch_user_page_flags(uintptr_t address) {
  uint64_t *entry = page_entry(arch_address_space_current(), address, false);
  if (!entry || (*entry & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
    return 0;
  return VM_READ | (*entry & (PTE_WRITE | PTE_COW) ? VM_WRITE : 0) |
         (*entry & PTE_NX ? 0 : VM_EXEC);
}

uintptr_t arch_user_find_free(uintptr_t lower, uintptr_t upper, size_t size) {
  size_t available = 0;
  for (uintptr_t page = upper; page > lower;) {
    page -= PAGE_BYTES;
    available = arch_user_page_flags(page) ? 0 : available + PAGE_BYTES;
    if (available == size)
      return page;
  }
  return 0;
}

static bool user_pages_change(uintptr_t address, size_t size,
                              unsigned protection, bool unmap) {
  arch_address_space_t root = arch_address_space_current();
  for (size_t offset = 0; offset < size; offset += PAGE_BYTES) {
    uint64_t *entry = page_entry(root, address + offset, false);
    if (!entry ||
        (*entry & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER) ||
        (*entry & PTE_DEVICE) || (!unmap && (*entry & PTE_SHARED)))
      return false;
  }
  for (size_t offset = 0; offset < size; offset += PAGE_BYTES) {
    uint64_t *entry = page_entry(root, address + offset, false);
    if (unmap) {
      mapping_release(*entry);
      *entry = 0;
    } else {
      *entry = (*entry & ~(PTE_WRITE | PTE_COW | PTE_NX)) |
               (protection & VM_EXEC ? 0 : PTE_NX);
      if (protection & VM_WRITE)
        *entry |= pages[(*entry & PTE_ADDRESS) / PAGE_BYTES].references > 1
                      ? PTE_COW
                      : PTE_WRITE;
    }
  }
  if (size)
    x64_tlb_invalidate(root, address, size);
  return true;
}
bool arch_user_protect(uintptr_t address, size_t size, unsigned protection) {
  return user_pages_change(address, size, protection, false);
}
bool arch_user_unmap(uintptr_t address, size_t size) {
  return user_pages_change(address, size, 0, true);
}

page_fault_result_t arch_page_fault_resolve(uintptr_t address, uint32_t error) {
  if (error != 3 && error != 7)
    return PAGE_FAULT_UNHANDLED;
  arch_address_space_t root = arch_address_space_current();
  uint64_t *entry = page_entry(root, address, false);
  if (!entry || !(*entry & PTE_COW))
    return PAGE_FAULT_UNHANDLED;
  uint64_t physical = *entry & PTE_ADDRESS;
  if (pages[physical / PAGE_BYTES].references > 1) {
    void *copy = user_page_allocate();
    if (!copy)
      return PAGE_FAULT_NO_MEMORY;
    memcpy(copy, x64_physical_pointer(physical), PAGE_BYTES);
    page_release(physical);
    *entry = (*entry & ~PTE_ADDRESS) | x64_virtual_physical(copy);
  }
  *entry = (*entry & ~PTE_COW) | PTE_WRITE;
  x64_tlb_invalidate(root, address, PAGE_BYTES);
  return PAGE_FAULT_RESOLVED;
}

bool arch_address_space_share(uintptr_t source, uintptr_t target, size_t size,
                              arch_address_space_t from,
                              arch_address_space_t to) {
  if (!size || ((source | target | size) & 4095) || source < USER_SPACE_START ||
      source >= USER_HEAP_END || size > USER_HEAP_END - source ||
      target < USER_SHARED_START || target >= USER_SHARED_END ||
      size > USER_SHARED_END - target)
    return false;
  /* Prepare all destination tables before publishing any shared mappings. */
  for (size_t offset = 0; offset < size; offset += PAGE_BYTES) {
    uint64_t *src = page_entry(from, source + offset, false);
    uint64_t *dst = page_entry(to, target + offset, true);
    if (!src || !dst ||
        (*src & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER) ||
        (*src & PTE_DEVICE) || !(*src & PTE_NX) ||
        !(*src & (PTE_WRITE | PTE_COW)) || (*dst & PTE_PRESENT))
      return false;
  }
  size_t offset = 0;
  for (; offset < size; offset += PAGE_BYTES) {
    uint64_t *src = page_entry(from, source + offset, false);
    if (*src & PTE_COW) {
      void *copy = user_page_allocate();
      if (!copy)
        break;
      memcpy(copy, x64_physical_pointer(*src & PTE_ADDRESS), PAGE_BYTES);
      mapping_release(*src);
      *src = x64_virtual_physical(copy) | PTE_PRESENT | PTE_USER | PTE_WRITE |
             PTE_NX;
    }
    *src |= PTE_SHARED | PTE_WRITE;
    page_retain(*src & PTE_ADDRESS);
    *page_entry(to, target + offset, false) = *src;
  }
  if (offset) {
    x64_tlb_invalidate(from, source, offset);
    x64_tlb_invalidate(to, target, offset);
  }
  if (offset == size)
    return true;
  arch_address_space_unmap_shared(target, offset, to);
  return false;
}

bool arch_address_space_unmap_shared(uintptr_t target, size_t size,
                                     arch_address_space_t root) {
  if ((target | size) & 4095 || target < USER_SHARED_START ||
      target >= USER_SHARED_END || size > USER_SHARED_END - target)
    return false;
  for (size_t offset = 0; offset < size; offset += PAGE_BYTES) {
    uint64_t *entry = page_entry(root, target + offset, false);
    if (entry && (*entry & PTE_SHARED)) {
      mapping_release(*entry);
      *entry = 0;
    }
  }
  if (size)
    x64_tlb_invalidate(root, target, size);
  return true;
}

bool arch_address_space_map_user_device(uintptr_t user, uintptr_t physical,
                                        size_t size) {
  if (!size || ((user | physical | size) & 4095) ||
      user < USER_FRAMEBUFFER_START || user >= USER_SPACE_END ||
      size > USER_SPACE_END - user)
    return false;
  arch_address_space_t root = arch_address_space_current();
  size_t offset = 0;
  for (; offset < size; offset += PAGE_BYTES) {
    uint64_t *entry = page_entry(root, user + offset, true);
    if (!entry)
      break;
    mapping_release(*entry);
    uint64_t cache_flags = PTE_UNCACHED;
    /* Keep the kernel and user aliases consistent, including Limine's PAT/WC
     * framebuffer mapping. A large-page PAT bit moves when creating a PTE. */
    virtual_translate(x64_physical_pointer(physical + offset), &cache_flags);
    *entry = (physical + offset) | PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_NX |
             PTE_DEVICE | cache_flags;
  }
  if (offset)
    x64_tlb_invalidate(root, user, offset);
  return offset == size;
}

void *arch_mmio_map(uint64_t physical, size_t size) {
  if (!size || physical >= USER_SPACE_END || size > USER_SPACE_END - physical ||
      physical + size - 1 > UINTPTR_MAX - x64_hhdm)
    return NULL;
  uintptr_t start = (physical & ~(uint64_t)4095) + x64_hhdm;
  uintptr_t last = (physical + size - 1) & ~(uint64_t)4095;
  uint64_t p = physical & ~(uint64_t)4095;
  bool changed = false;
  for (; p <= last; p += PAGE_BYTES) {
    uintptr_t address = x64_hhdm + p;
    if (x64_virtual_physical((void *)address) == p)
      continue;
    uint64_t *entry = page_entry(x64_kernel_cr3, address, true);
    if (!entry)
      break;
    *entry = p | PTE_PRESENT | PTE_WRITE | PTE_NX | PTE_UNCACHED;
    changed = true;
  }
  if (changed)
    x64_tlb_invalidate(x64_kernel_cr3, start, size);
  return p > last ? (void *)(start + (physical & 4095)) : NULL;
}

/* Fresh virtual addresses prevent a reloaded module from inheriting another
 * CPU's old instruction translations. Physical storage is still reclaimed. */
static uintptr_t module_next = 0xffffa00000000000ull;
void *arch_module_allocate(size_t size) {
  if (!size || size > INT_MAX || module_next > 0xffffbffffffff000ull - size)
    return NULL;
  uintptr_t start = module_next;
  size = (size + 4095) & ~(size_t)4095;
  module_next += size;
  for (size_t offset = 0; offset < size; offset += PAGE_BYTES) {
    uint64_t *entry = page_entry(x64_kernel_cr3, start + offset, true);
    void *page = entry ? page_malloc_one_no_mark() : NULL;
    if (!page) {
      arch_module_free((void *)start, offset);
      return NULL;
    }
    *entry = x64_virtual_physical(page) | PTE_PRESENT | PTE_WRITE | PTE_NX;
  }
  return (void *)start;
}
bool arch_module_protect(void *address, size_t size, bool writable,
                         bool executable) {
  if (writable && executable)
    return false;
  size_t offset = 0;
  for (; offset < size; offset += PAGE_BYTES) {
    uintptr_t virtual = (uintptr_t)address + offset;
    uint64_t *entry = page_entry(x64_kernel_cr3, virtual, false);
    if (!entry || !(*entry & PTE_PRESENT))
      break;
    *entry = (*entry & ~(PTE_WRITE | PTE_NX)) | (writable ? PTE_WRITE : 0) |
             (executable ? 0 : PTE_NX);
  }
  if (offset)
    x64_tlb_invalidate(x64_kernel_cr3, (uintptr_t)address, offset);
  return offset >= size;
}
void arch_module_free(void *address, size_t size) {
  for (size_t offset = 0; offset < size; offset += PAGE_BYTES) {
    uintptr_t virtual = (uintptr_t)address + offset;
    uint64_t *entry = page_entry(x64_kernel_cr3, virtual, false);
    if (!entry || !(*entry & PTE_PRESENT))
      continue;
    page_release(*entry & PTE_ADDRESS);
    *entry = 0;
  }
  if (size)
    x64_tlb_invalidate(x64_kernel_cr3, (uintptr_t)address, size);
}

void init_page(const boot_info_t *info) {
  page_count = arch_memory_detect(info) / PAGE_BYTES;
  user_hint = page_count;
  size_t bytes = (page_count * sizeof(*pages) + 4095) & ~(size_t)4095;
  for (size_t i = 0; i < info->memory_range_count; i++) {
    boot_memory_range_t *range = &info->memory_ranges[i];
    if (range->type != BOOT_MEMORY_USABLE || range->length < bytes)
      continue;
    pages = x64_physical_pointer(range->base);
    range->base += bytes;
    range->length -= bytes;
    break;
  }
  if (!pages)
    page_panic();
  for (size_t i = 0; i < page_count; i++)
    pages[i] = (physical_page_t){UINT_MAX, 0};
  for (size_t i = 0; i < info->memory_range_count; i++) {
    const boot_memory_range_t *range = &info->memory_ranges[i];
    if (range->type != BOOT_MEMORY_USABLE)
      continue;
    size_t first = (range->base + 4095) / PAGE_BYTES;
    size_t end = (range->base + range->length) / PAGE_BYTES;
    for (size_t j = first; j < end; j++)
      pages[j].references = 0;
  }
  uint64_t old_cr3 = arch_address_space_current();
  uint64_t *root = page_malloc_one_no_mark();
  if (!root)
    page_panic();
  memcpy(root + 256, (uint64_t *)x64_physical_pointer(old_cr3) + 256,
         PAGE_BYTES / 2);
  /* All processes share the upper-level kernel tables, including later MMIO. */
  for (size_t i = 256; i < TABLE_ENTRIES; i++) {
    if (root[i] & PTE_PRESENT)
      continue;
    void *table = page_malloc_one_no_mark();
    if (!table)
      page_panic();
    root[i] = x64_virtual_physical(table) | PTE_PRESENT | PTE_WRITE;
  }
  x64_kernel_cr3 = x64_virtual_physical(root);
  x64_msr_write(0xc0000080, x64_msr_read(0xc0000080) | (1ull << 11));
  arch_address_space_activate(x64_kernel_cr3);
  uint64_t cr0;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | (1ull << 16)) : "memory");
}
void pf_set(uintptr_t size) { (void)size; /* Limine's map is authoritative. */ }
