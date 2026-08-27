#include <arch/x86/control.h>
#include <dos.h>
#include <irq.h>
#include <kasan.h>
#define MALLOC_ALIGN 8

typedef struct {
  int size;
  int offset;
} malloc_header;
static inline uintptr_t align_up_uintptr(uintptr_t value, uintptr_t align) {
  return (value + align - 1) & ~(align - 1);
}

/* 写入 pattern、两次取反回读，最后恢复原值；返回该 dword 是否为真实内存。 */
static bool memory_probe_dword(volatile uint32_t *probe) {
  const uint32_t pattern = 0xaa55aa55u;
  uint32_t saved = *probe;

  *probe = pattern;
  *probe ^= 0xffffffffu;
  bool usable = *probe == ~pattern;
  if (usable) {
    *probe ^= 0xffffffffu;
    usable = *probe == pattern;
  }
  *probe = saved;
  return usable;
}

/* 从 1GiB 的步长开始向上探测，失败就把步长缩到 1/4，最小 4KiB，
 * 返回第一个不可用地址，即可用内存的上界。 */
static unsigned int memory_probe_limit(unsigned int start, unsigned int end) {
  unsigned int address = start;

  for (unsigned int block = 1024u * 1024u * 1024u; block >= 0x1000u;) {
    volatile uint32_t *probe =
        (volatile uint32_t *)(address + block - sizeof(uint32_t));
    if (!memory_probe_dword(probe)) {
      block /= 4;
      continue;
    }
    address += block;
    if (address > end) {
      break;
    }
  }
  return address;
}

unsigned int memtest(unsigned int start, unsigned int end) {
  /* 386 无法把 AC 位置 1，只有 486 及以上才支持并需要临时关闭缓存。 */
  uint32_t eflags = x86_eflags_read();
  x86_eflags_write(eflags | X86_EFLAGS_AC);
  bool cache_control = (x86_eflags_read() & X86_EFLAGS_AC) != 0;
  x86_eflags_write(eflags & ~X86_EFLAGS_AC);

  irq_state_t state = irq_save();
  if (cache_control) {
    x86_cr0_write(x86_cr0_read() | X86_CR0_CD | X86_CR0_NW);
  }
  unsigned int limit = memory_probe_limit(start, end);
  if (cache_control) {
    x86_cr0_write(x86_cr0_read() & ~(X86_CR0_CD | X86_CR0_NW));
  }
  irq_restore(state);
  return limit;
}

void swap(free_member *a, free_member *b) {
  free_member temp = *a;
  *a = *b;
  *b = temp;
}
int cmp(free_member a, free_member b) { return a.end <= b.end; }
int partition(free_member *arr, int low, int high) {
  free_member pivot = arr[high];
  int i = (low - 1);

  for (int j = low; j <= high - 1; j++) {
    if (cmp(arr[j], pivot)) {
      i++;
      swap(&arr[i], &arr[j]);
    }
  }

  swap(&arr[i + 1], &arr[high]);
  return (i + 1);
}

void quicksort(free_member *arr, int low, int high) {
  if (low < high) {
    int pi = partition(arr, low, high);
    quicksort(arr, low, pi - 1);
    quicksort(arr, pi + 1, high);
  }
}
freeinfo *make_next_freeinfo(memory *mem) {
  const int size = FREE_MAX_NUM * sizeof(free_member) + sizeof(freeinfo);
  freeinfo *fi = NULL;
  freeinfo *finf = mem->freeinf;
  freeinfo *old = NULL;
  uintptr_t s, n;
  while (finf) {
    old = finf;
    for (int i = 0; i < FREE_MAX_NUM; i++) {
      if (finf->f[i].start + finf->f[i].end == 0) {
        break;
      }
      if (finf->f[i].end - finf->f[i].start >= size) {
        uintptr_t start = finf->f[i].start;
        s = finf->f[i].start;
        n = finf->f[i].end;
        mem_delete(i, finf);
        fi = (freeinfo *)start;
        break;
      }
    }
    if (fi) {
      break;
    }
    finf = finf->next;
  }
  if (!fi) {
    mem->memerrno = ERRNO_NO_ENOGHT_MEMORY;
    return NULL;
  }
  fi->next = 0;
  while (finf) {
    old = finf;
    finf = finf->next;
  }
  old->next = fi;
  fi->f = (free_member *)((uintptr_t)fi + sizeof(freeinfo));
  for (int i = 0; i < FREE_MAX_NUM; i++) {
    fi->f[i].start = 0;
    fi->f[i].end = 0;
  }

  if (n - s > size) {
    mem_free_finf(mem, fi, (void *)(uintptr_t)(s + size), n - s - size); // 一点也不浪费
  }

  return fi;
}
free_member *mem_insert(int pos, freeinfo *finf) {
  int j = 0;
  for (int i = 0; i < FREE_MAX_NUM; i++) {
    if (finf->f[i].start + finf->f[i].end != 0) {
      ++j;
    }
  }
  if (j == FREE_MAX_NUM) {
    return NULL;
  }
  for (int i = j - 1; i >= pos; i--) {
    unsigned debug1 = (unsigned)(&(finf->f[i + 1]));
    unsigned debug2 = (unsigned)(&(finf->f[i]));
    if (!debug1 || !debug2) {
      printk("error!\n");
      for (;;)
        ;
    }
    finf->f[i + 1] = finf->f[i];
  }
  return &(finf->f[pos]);
}
free_member *mem_add(freeinfo *finf) {
  int j = -1;
  for (int i = 0; i < FREE_MAX_NUM; i++) {
    if (finf->f[i].start + finf->f[i].end == 0) {
      j = i;
      break;
    }
  }
  if (j == -1) {
    return NULL;
  }
  return &(finf->f[j]);
}
void mem_delete(int pos, freeinfo *finf) {
  int i;
  for (i = pos; i < FREE_MAX_NUM - 1; i++) {
    if (finf->f[i].start == 0 && finf->f[i].end == 0) {
      return;
    }
    finf->f[i] = finf->f[i + 1];
  }
  finf->f[i].start = 0;
  finf->f[i].end = 0;
}
uint32_t mem_get_all_finf(freeinfo *finf) {
  for (int i = 0; i < FREE_MAX_NUM; i++) {
    if (finf->f[i].start + finf->f[i].end == 0) {
      return i;
    }
  }
  return FREE_MAX_NUM;
}
// 内存整理
void mem_defragmenter(freeinfo *finf) {
  for (int i = 0; i < FREE_MAX_NUM - 1; i++) {
    if (finf->f[i].start + finf->f[i].end == 0) {
      break;
    }
    if (finf->f[i].end - finf->f[i].start == 0) {
      mem_delete(i, finf);
      continue;
    }
    if (finf->f[i].end == finf->f[i + 1].start) {
      int end = finf->f[i + 1].end;
      mem_delete(i + 1, finf);
      finf->f[i].end = end;
      continue;
    }
    if (finf->f[i + 1].start == finf->f[i].start) {
      int end = MEM_MAX(finf->f[i].end, finf->f[i + 1].end);
      mem_delete(i + 1, finf);
      finf->f[i].end = end;
      continue;
    }
    if (finf->f[i + 1].start < finf->f[i].end) {
      int end = MEM_MAX(finf->f[i].end, finf->f[i + 1].end);
      mem_delete(i + 1, finf);
      finf->f[i].end = end;
      continue;
    }
  }
}
int mem_free_finf(memory *mem, freeinfo *finf, void *p, uint32_t size) {
  quicksort(finf->f, 0, mem_get_all_finf(finf) - 1);
  mem_defragmenter(finf);
  free_member *tmp1 = NULL, // 第一（二）个连续的内存 其limit与start相等
      *tmp2 = NULL; // 第二（一）个连续的内存  其start与limit相等
  int idx1, idx2;
  // 遍历内存池，找到符合条件的两个格子（找不到也没关系）

  for (int i = 0; i < FREE_MAX_NUM; i++) {
    uintptr_t current_start = (uintptr_t)finf->f[i].start;
    uintptr_t current_end = (uintptr_t)finf->f[i].end;
    uintptr_t ptr_val = (uintptr_t)p;

    if (current_end == ptr_val) {
      tmp1 = &(finf->f[i]);
      idx1 = i;
    }
    if (current_start == ptr_val + size) {
      tmp2 = &(finf->f[i]);
      idx2 = i;
    }
  }

  if (!tmp1 && !tmp2) {             // 没有内存和他连续
                                    // for(;;);
    free_member *n = mem_add(finf); // 找一个空闲的格子放这块内存
    if (!n)
      return 0;
    // 配置这个格子
    n->start = (uintptr_t)p;
    n->end = (uintptr_t)p + size;
    quicksort(finf->f, 0, mem_get_all_finf(finf) - 1);
    mem_defragmenter(finf);
    return 1;
  }
  // for(;;);
  //  两个都找到了，说明是个缺口
  if (tmp1 && tmp2) {
    tmp1->end = tmp2->end;
    mem_delete(idx2, finf);
    quicksort(finf->f, 0, mem_get_all_finf(finf) - 1);
    mem_defragmenter(finf);
    return 1;
  }
  if (tmp1) { // BUGFIX
    tmp1->end += size;
    quicksort(finf->f, 0, mem_get_all_finf(finf) - 1);
    mem_defragmenter(finf);
    return 1;
  }
  if (tmp2) {
    tmp2->start = (uintptr_t)p;
    quicksort(finf->f, 0, mem_get_all_finf(finf) - 1);
    mem_defragmenter(finf);
    return 1;
  }

  return 1;
}
void *mem_alloc_finf(memory *mem, freeinfo *finf, uint32_t size,
                     freeinfo *if_nomore) {
  free_member *choice = NULL;
  int choice_index = 0;
  for (int i = 0; i < FREE_MAX_NUM; i++) {
    if (finf->f[i].start == 0 && finf->f[i].end == 0) {
      break;
    }
    if (finf->f[i].end - finf->f[i].start >= size) {
      if (!choice) {
        choice = &(finf->f[i]);
        choice_index = i;
        continue;
      }
      if (finf->f[i].end - finf->f[i].start < choice->start - choice->end) {
        choice = &(finf->f[i]);
        choice_index = i;
        continue;
      }
    }
  }
  if (choice == NULL) {
    mem->memerrno = ERRNO_NO_ENOGHT_MEMORY;
    return NULL;
  }
  uintptr_t start = choice->start;
  choice->start += size;
  if (choice->end - choice->start == 0) {
    mem_delete(choice_index, finf);
  }
  mem->memerrno = ERRNO_NOPE;
  mem_defragmenter(finf);
  memset((void *)start, 0, size);

  return (void *)start;
}
void *mem_alloc(memory *mem, uint32_t size) {
  freeinfo *finf = mem->freeinf;
  int flag = 0;
  freeinfo *if_nomore = NULL;
  while (finf) {
    if (flag && !if_nomore) {
      break;
      ;
    }
    void *result = mem_alloc_finf(mem, finf, size, if_nomore);
    if (mem->memerrno != ERRNO_NOPE) {
      if (mem->memerrno == ERRNO_NO_MORE_FREE_MEMBER) {
        if (!flag) {
          if_nomore = finf;
          flag = 1;
        }
      }
    } else {
      return result;
    }
    if (flag) {
      if_nomore = if_nomore->next;
    } else {
      finf = finf->next;
    }
  }
  if (flag) {
    freeinfo *new_f = make_next_freeinfo(mem);
    if (!new_f) {
      return NULL;
    }
    return mem_alloc(mem, size);
  }
  return NULL;
}
void mem_free(memory *mem, void *p, uint32_t size) {
  freeinfo *finf = mem->freeinf;
  while (finf) {
    if (mem_free_finf(mem, finf, p, size)) {
      return;
    }
    finf = finf->next;
  }
  freeinfo *new_f = make_next_freeinfo(mem);
  if (new_f) {
    mem_free_finf(mem, new_f, p, size);
  }
}
void show_mem(memory *mem) {
  printk("----------------\n");
  freeinfo *finf = mem->freeinf;
  while (finf) {
    for (int i = 0; i < FREE_MAX_NUM; i++) {
      if (finf->f[i].start == 0 && finf->f[i].end == 0) {
        break;
      }
      printk("START: %08x END: %08x SIZE: %08x Bytes\n", finf->f[i].start,
             finf->f[i].end, finf->f[i].end - finf->f[i].start);
    }
    finf = finf->next;
  }
  printk("----------------\n");
}
memory *memory_init(uintptr_t start, uint32_t size) {
  memory *mem;
  mem = (memory *)start;
  start += sizeof(memory);
  size -= sizeof(memory);
  if (size < 0) {
    printk("mm init error.\n");
    for (;;)
      ;
  }
  mem->freeinf = (freeinfo *)start;
  start += sizeof(freeinfo);
  size -= sizeof(freeinfo);
  if (size < 0) {
    printk("mm init error.\n");
    for (;;)
      ;
  }
  mem->freeinf->next = 0;
  mem->freeinf->f = (free_member *)start;
  start += FREE_MAX_NUM * sizeof(free_member);
  size -= FREE_MAX_NUM * sizeof(free_member);
  if ((int)size < 0) {
    printk("mm init error.\n");
    for (;;)
      ;
  }
  for (int i = 0; i < FREE_MAX_NUM; i++) {
    mem->freeinf->f[i].start = 0;
    mem->freeinf->f[i].end = 0;
  }
  mem->memerrno = ERRNO_NOPE;
  mem_free(mem, (void *)start, size);
  return mem;
}
extern memory *public_heap;
void *malloc(int size) {
  uint32_t total_size;
  uintptr_t raw =
      (uintptr_t)mem_alloc(public_heap, size + sizeof(malloc_header) +
                                            (MALLOC_ALIGN - 1) +
                                            KASAN_HEAP_RIGHT_REDZONE);
  if (raw == 0)
    return NULL;
  total_size = size + sizeof(malloc_header) + (MALLOC_ALIGN - 1) +
               KASAN_HEAP_RIGHT_REDZONE;
  uintptr_t user = align_up_uintptr(raw + sizeof(malloc_header), MALLOC_ALIGN);
  malloc_header *header = (malloc_header *)(user - sizeof(malloc_header));
  header->size = size;
  header->offset = (int)(user - raw);
  kasan_alloc((void *)raw, total_size, (void *)user, (uint32_t)size,
              KASAN_ALLOC_MALLOC);
  return (void *)user;
}
void free(void *p) {
  if (p == NULL)
    return;
  malloc_header *header = (malloc_header *)((uintptr_t)p - sizeof(malloc_header));
  uintptr_t raw = (uintptr_t)p - header->offset;
  kasan_free(p, KASAN_ALLOC_MALLOC);
  mem_free(public_heap, (void *)raw,
           header->size + sizeof(malloc_header) + (MALLOC_ALIGN - 1) +
               KASAN_HEAP_RIGHT_REDZONE);
}
void *realloc(void *ptr, uint32_t size) {
  void *new = malloc(size);
  if (ptr) {
    malloc_header *header =
        (malloc_header *)((uintptr_t)ptr - sizeof(malloc_header));
    uint32_t copy_size = header->size < (int)size ? header->size : size;
    memcpy(new, ptr, copy_size);
    free(ptr);
  }
  return new;
}

void *kmalloc(int size) {
  uint32_t total_size;
  uintptr_t raw = (uintptr_t)page_malloc(size + sizeof(malloc_header) +
                                         (MALLOC_ALIGN - 1) +
                                         KASAN_HEAP_RIGHT_REDZONE);
  if (raw == 0)
    return NULL;
  total_size = size + sizeof(malloc_header) + (MALLOC_ALIGN - 1) +
               KASAN_HEAP_RIGHT_REDZONE;
  uintptr_t user = align_up_uintptr(raw + sizeof(malloc_header), MALLOC_ALIGN);
  malloc_header *header = (malloc_header *)(user - sizeof(malloc_header));
  header->size = size;
  header->offset = (int)(user - raw);
  kasan_alloc((void *)raw, total_size, (void *)user, (uint32_t)size,
              KASAN_ALLOC_KMALLOC);
  return (void *)user;
}
void kfree(void *p) {
  if (p == NULL)
    return;
  malloc_header *header = (malloc_header *)((uintptr_t)p - sizeof(malloc_header));
  uintptr_t raw = (uintptr_t)p - header->offset;
  kasan_free(p, KASAN_ALLOC_KMALLOC);
  page_free((void *)raw,
            header->size + sizeof(malloc_header) + (MALLOC_ALIGN - 1) +
                KASAN_HEAP_RIGHT_REDZONE);
}
void* krealloc(void* ptr, uint32_t size) {
  void* new = kmalloc(size);
  if (ptr) {
    malloc_header *header =
        (malloc_header *)((uintptr_t)ptr - sizeof(malloc_header));
    uint32_t copy_size = header->size < (int)size ? header->size : size;
    memcpy(new, ptr, copy_size);
    kfree(ptr);
  }
  return new;
}
