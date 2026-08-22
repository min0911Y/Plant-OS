#include <dos.h>
#include <kasan.h>

#if !KASAN_ENABLED

void kasan_init(void) {}
void kasan_poison(void *addr, uint32_t size, uint8_t tag) {
  (void)addr;
  (void)size;
  (void)tag;
}
void kasan_unpoison(void *addr, uint32_t size) {
  (void)addr;
  (void)size;
}
void kasan_alloc(void *base, uint32_t total_size, void *user,
                 uint32_t user_size, uint8_t type) {
  (void)base;
  (void)total_size;
  (void)user;
  (void)user_size;
  (void)type;
}
void kasan_free(void *user, uint8_t type) {
  (void)user;
  (void)type;
}
void kasan_page_alloc(void *base, uint32_t total_size, uint32_t requested_size) {
  (void)base;
  (void)total_size;
  (void)requested_size;
}
void kasan_page_free(void *base, uint32_t total_size) {
  (void)base;
  (void)total_size;
}
int kasan_check_range(const void *addr, uint32_t size, int write) {
  (void)addr;
  (void)size;
  (void)write;
  return 1;
}
void kasan_report(const void *addr, uint32_t size, int write,
                  const char *reason) {
  (void)addr;
  (void)size;
  (void)write;
  (void)reason;
}
void __asan_load1_noabort(uintptr_t addr) { (void)addr; }
void __asan_load2_noabort(uintptr_t addr) { (void)addr; }
void __asan_load4_noabort(uintptr_t addr) { (void)addr; }
void __asan_load8_noabort(uintptr_t addr) { (void)addr; }
void __asan_load16_noabort(uintptr_t addr) { (void)addr; }
void __asan_loadN_noabort(uintptr_t addr, uint32_t size) {
  (void)addr;
  (void)size;
}
void __asan_store1_noabort(uintptr_t addr) { (void)addr; }
void __asan_store2_noabort(uintptr_t addr) { (void)addr; }
void __asan_store4_noabort(uintptr_t addr) { (void)addr; }
void __asan_store8_noabort(uintptr_t addr) { (void)addr; }
void __asan_store16_noabort(uintptr_t addr) { (void)addr; }
void __asan_storeN_noabort(uintptr_t addr, uint32_t size) {
  (void)addr;
  (void)size;
}
void __asan_handle_no_return(void) {}

#else

#define KASAN_MAX_ALLOCS 16384u
#define KASAN_STATE_EMPTY 0u
#define KASAN_STATE_ACTIVE 1u
#define KASAN_STATE_FREED 2u
#define KASAN_TAG_VALID 0u
#define KASAN_TAG_LEFT 0xf1u
#define KASAN_TAG_RIGHT 0xf2u
#define KASAN_TAG_FREED 0xf3u
#define KASAN_TAG_PAGE_REDZONE 0xf4u
#define KASAN_TAG_UNTRACKED 0xffu

struct kasan_alloc_info {
  uintptr_t base;
  uintptr_t user;
  uint32_t total_size;
  uint32_t user_size;
  uint8_t type;
  uint8_t state;
};

static uint8_t *const kasan_shadow = (uint8_t *)KASAN_SHADOW_START;
static struct kasan_alloc_info kasan_allocs[KASAN_MAX_ALLOCS];
static int kasan_ready;
static int kasan_reporting;
static uintptr_t kasan_last_pc;

static void kasan_raw_fill(void *addr, int val, uint32_t size) KASAN_NOINSTR;
static struct kasan_alloc_info *kasan_find_by_user(uintptr_t user,
                                                    uint8_t type) KASAN_NOINSTR;
static struct kasan_alloc_info *kasan_find_containing(uintptr_t addr,
                                                       uint32_t size)
    KASAN_NOINSTR;
static uint8_t kasan_tag_for_addr(uintptr_t addr) KASAN_NOINSTR;
static void kasan_poison_partial(uintptr_t user, uint32_t size) KASAN_NOINSTR;

static void kasan_raw_fill(void *addr, int val, uint32_t size) {
  unsigned char byte = (unsigned char)val;
  uint32_t pattern = byte;
  uint32_t count = size / 4u;
  uint32_t tail = size & 3u;
  unsigned char *p;

  pattern |= pattern << 8;
  pattern |= pattern << 16;

  asm volatile("cld; rep stosl"
               : "+D"(addr), "+c"(count)
               : "a"(pattern)
               : "memory");

  p = (unsigned char *)addr;
  while (tail--) {
    *p++ = byte;
  }
}

void kasan_init(void) {
  kasan_raw_fill(kasan_shadow, KASAN_TAG_UNTRACKED, KASAN_SHADOW_SIZE);
  kasan_raw_fill(kasan_allocs, 0, sizeof(kasan_allocs));
  kasan_ready = 1;
}

void kasan_poison(void *addr, uint32_t size, uint8_t tag) {
  uintptr_t start = (uintptr_t)addr / KASAN_SHADOW_GRANULE;
  uintptr_t end = ((uintptr_t)addr + size + KASAN_SHADOW_GRANULE - 1u) /
                  KASAN_SHADOW_GRANULE;

  if (!kasan_ready || size == 0) {
    return;
  }
  if (start >= KASAN_SHADOW_SIZE) {
    return;
  }
  if (end > KASAN_SHADOW_SIZE) {
    end = KASAN_SHADOW_SIZE;
  }
  kasan_raw_fill(&kasan_shadow[start], tag, end - start);
}

void kasan_unpoison(void *addr, uint32_t size) {
  kasan_poison(addr, size, KASAN_TAG_VALID);
  kasan_poison_partial((uintptr_t)addr, size);
}

static void kasan_poison_partial(uintptr_t user, uint32_t size) {
  uintptr_t shadow;
  uint32_t valid;

  if (!kasan_ready || size == 0) {
    return;
  }

  valid = (user + size) & (KASAN_SHADOW_GRANULE - 1u);
  if (valid == 0) {
    return;
  }

  shadow = (user + size) / KASAN_SHADOW_GRANULE;
  if (shadow < KASAN_SHADOW_SIZE) {
    kasan_shadow[shadow] = (uint8_t)valid;
  }
}

void kasan_alloc(void *base, uint32_t total_size, void *user,
                 uint32_t user_size, uint8_t type) {
  struct kasan_alloc_info *slot = NULL;
  uintptr_t user_addr = (uintptr_t)user;

  if (!kasan_ready || base == NULL || user == NULL || total_size == 0) {
    return;
  }

  for (uint32_t i = 0; i < KASAN_MAX_ALLOCS; i++) {
    if (kasan_allocs[i].state == KASAN_STATE_FREED &&
        kasan_allocs[i].base == (uintptr_t)base &&
        kasan_allocs[i].type == type) {
      slot = &kasan_allocs[i];
      break;
    }
  }
  if (slot == NULL) {
    for (uint32_t i = 0; i < KASAN_MAX_ALLOCS; i++) {
      if (kasan_allocs[i].state == KASAN_STATE_EMPTY) {
        slot = &kasan_allocs[i];
        break;
      }
    }
  }
  if (slot == NULL) {
    for (uint32_t i = 0; i < KASAN_MAX_ALLOCS; i++) {
      if (kasan_allocs[i].state == KASAN_STATE_FREED) {
        slot = &kasan_allocs[i];
        break;
      }
    }
  }
  if (slot == NULL) {
    kasan_report(user, user_size, 0, "metadata table full");
    return;
  }

  slot->base = (uintptr_t)base;
  slot->user = user_addr;
  slot->total_size = total_size;
  slot->user_size = user_size;
  slot->type = type;
  slot->state = KASAN_STATE_ACTIVE;

  kasan_poison(base, total_size, KASAN_TAG_RIGHT);
  if (user_addr > (uintptr_t)base) {
    kasan_poison(base, user_addr - (uintptr_t)base, KASAN_TAG_LEFT);
  }
  kasan_unpoison(user, user_size);
}

void kasan_free(void *user, uint8_t type) {
  struct kasan_alloc_info *info;

  if (!kasan_ready || user == NULL) {
    return;
  }

  info = kasan_find_by_user((uintptr_t)user, type);
  if (info == NULL) {
    kasan_report(user, 1, 1, "invalid free");
    return;
  }
  if (info->state == KASAN_STATE_FREED) {
    kasan_report(user, info->user_size, 1, "double free");
    return;
  }

  kasan_poison((void *)info->base, info->total_size, KASAN_TAG_FREED);
  info->state = KASAN_STATE_FREED;
}

void kasan_page_alloc(void *base, uint32_t total_size,
                      uint32_t requested_size) {
  uintptr_t redzone_start;
  uintptr_t redzone_end;

  if (!kasan_ready || base == NULL || total_size == 0) {
    return;
  }
  if (requested_size > total_size) {
    requested_size = total_size;
  }

  kasan_poison(base, total_size, KASAN_TAG_PAGE_REDZONE);
  kasan_unpoison(base, requested_size);
  redzone_start = ((uintptr_t)base + requested_size +
                   KASAN_SHADOW_GRANULE - 1u) &
                  ~(KASAN_SHADOW_GRANULE - 1u);
  redzone_end = (uintptr_t)base + total_size;
  if (redzone_start < redzone_end) {
    kasan_poison((void *)redzone_start, redzone_end - redzone_start,
                 KASAN_TAG_PAGE_REDZONE);
  }
}

void kasan_page_free(void *base, uint32_t total_size) {
  uintptr_t start = (uintptr_t)base;
  uintptr_t end = start + total_size;

  if (!kasan_ready || base == NULL || total_size == 0) {
    return;
  }
  if (end < start) {
    kasan_report(base, total_size, 1, "page free overflow");
    return;
  }
  kasan_poison(base, total_size, KASAN_TAG_FREED);
}

static struct kasan_alloc_info *kasan_find_by_user(uintptr_t user,
                                                    uint8_t type) {
  struct kasan_alloc_info *freed = NULL;

  for (uint32_t i = 0; i < KASAN_MAX_ALLOCS; i++) {
    struct kasan_alloc_info *info = &kasan_allocs[i];

    if (info->state == KASAN_STATE_EMPTY || info->user != user ||
        info->type != type) {
      continue;
    }
    if (info->state == KASAN_STATE_ACTIVE) {
      return info;
    }
    freed = info;
  }

  return freed;
}

static struct kasan_alloc_info *kasan_find_containing(uintptr_t addr,
                                                       uint32_t size) {
  uintptr_t last = addr + size - 1u;
  struct kasan_alloc_info *best = NULL;

  for (uint32_t i = 0; i < KASAN_MAX_ALLOCS; i++) {
    struct kasan_alloc_info *info = &kasan_allocs[i];
    uintptr_t base = info->base;
    uintptr_t end = info->base + info->total_size;

    if (info->state != KASAN_STATE_ACTIVE) {
      continue;
    }
    if ((addr >= base && addr < end) || (last >= base && last < end)) {
      if (best == NULL || info->total_size < best->total_size) {
        best = info;
      }
    }
  }

  if (best != NULL) {
    return best;
  }

  for (uint32_t i = 0; i < KASAN_MAX_ALLOCS; i++) {
    struct kasan_alloc_info *info = &kasan_allocs[i];
    uintptr_t base = info->base;
    uintptr_t end = info->base + info->total_size;

    if (info->state == KASAN_STATE_EMPTY) {
      continue;
    }
    if ((addr >= base && addr < end) || (last >= base && last < end)) {
      best = info;
    }
  }

  return best;
}

static uint8_t kasan_tag_for_addr(uintptr_t addr) {
  uintptr_t shadow = addr / KASAN_SHADOW_GRANULE;

  if (shadow >= KASAN_SHADOW_SIZE) {
    return KASAN_TAG_UNTRACKED;
  }
  return kasan_shadow[shadow];
}

int kasan_check_range(const void *addr, uint32_t size, int write) {
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end;

  if (!kasan_ready || kasan_reporting || size == 0) {
    return 1;
  }

  end = start + size;
  if (end < start) {
    kasan_report(addr, size, write, "address overflow");
    return 0;
  }

  for (uintptr_t cur = start; cur < end; cur++) {
    uint8_t tag = kasan_tag_for_addr(cur);

    if (tag == KASAN_TAG_VALID || tag == KASAN_TAG_UNTRACKED) {
      continue;
    }
    if (tag > 0 && tag < KASAN_SHADOW_GRANULE &&
        (cur & (KASAN_SHADOW_GRANULE - 1u)) < tag) {
      continue;
    }

    if (tag == KASAN_TAG_FREED) {
      kasan_report((void *)cur, size, write, "use after free");
    } else {
      kasan_report((void *)cur, size, write, "redzone access");
    }
    return 0;
  }

  return 1;
}

void kasan_report(const void *addr, uint32_t size, int write,
                  const char *reason) {
  const char *kind = write ? "write" : "read";
  struct kasan_alloc_info *info;

  if (kasan_reporting) {
    return;
  }
  kasan_reporting = 1;

  logk("KASAN: invalid %s addr=%08x size=%d pc=%08x reason=%s\n", (char *)kind,
       (uint32_t)(uintptr_t)addr, size, (uint32_t)kasan_last_pc,
       (char *)reason);
  printk("KASAN: invalid %s addr=%08x size=%d pc=%08x reason=%s\n", kind,
         (uint32_t)(uintptr_t)addr, size, (uint32_t)kasan_last_pc, reason);

  info = kasan_find_containing((uintptr_t)addr, size ? size : 1u);
  if (info != NULL) {
    const char *state =
        info->state == KASAN_STATE_ACTIVE ? "active" : "freed";

    logk("KASAN: object base=%08x user=%08x user_size=%d total=%d type=%d "
         "state=%s\n",
         (uint32_t)info->base, (uint32_t)info->user, info->user_size,
         info->total_size, info->type, (char *)state);
    printk("KASAN: object base=%08x user=%08x user_size=%d total=%d type=%d "
           "state=%s\n",
           (uint32_t)info->base, (uint32_t)info->user, info->user_size,
           info->total_size, info->type, state);
  }

  io_cli();
  for (;;) {
    asm volatile("hlt");
  }
}

void __asan_load1_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 1, 0);
}
void __asan_load2_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 2, 0);
}
void __asan_load4_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 4, 0);
}
void __asan_load8_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 8, 0);
}
void __asan_load16_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 16, 0);
}
void __asan_loadN_noabort(uintptr_t addr, uint32_t size) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, size, 0);
}

void __asan_store1_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 1, 1);
}
void __asan_store2_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 2, 1);
}
void __asan_store4_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 4, 1);
}
void __asan_store8_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 8, 1);
}
void __asan_store16_noabort(uintptr_t addr) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, 16, 1);
}
void __asan_storeN_noabort(uintptr_t addr, uint32_t size) {
  kasan_last_pc = (uintptr_t)__builtin_return_address(0);
  kasan_check_range((void *)addr, size, 1);
}

void __asan_handle_no_return(void) {}

#endif
