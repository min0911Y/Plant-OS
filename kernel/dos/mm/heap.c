#include <dos.h>
#include <heap.h>
#include <irq.h>
#include <kasan.h>
#include <limits.h>

#define HEAP_ALIGNMENT (sizeof(uintptr_t) * 2u)
#define HEAP_PAGE_BYTES 4096u
#define HEAP_INITIAL_BYTES (1024u * 1024u)
#define HEAP_GROWTH_BYTES (1024u * 1024u)
#define HEAP_COOKIE 0x48454150u

typedef struct {
  size_t size;
  uintptr_t cookie;
} heap_allocation_header_t;

typedef enum {
  LIBALLOC_INVALID_FREE,
  LIBALLOC_LAYOUT_ERROR,
} liballoc_heap_error_t;

typedef void (*liballoc_error_handler_t)(liballoc_heap_error_t error,
                                         void *ptr);

bool liballoc_heap_init(uint8_t *address, size_t size);
bool liballoc_heap_extend(uint8_t *address, size_t size);
void liballoc_heap_onerror(liballoc_error_handler_t handler);
void *liballoc_aligned_alloc(size_t alignment, size_t size);
void liballoc_free(void *ptr);

static bool heap_ready;

static __attribute__((noreturn)) void heap_corruption(void *ptr) {
  Panic_K("kernel heap metadata corrupted ptr=%08x",
          (uint32_t)(uintptr_t)ptr);
  arch_halt();
}

static void liballoc_error(liballoc_heap_error_t error, void *ptr) {
  Panic_K("liballoc error=%u ptr=%08x", (uint32_t)error,
          (uint32_t)(uintptr_t)ptr);
  arch_halt();
}

static bool heap_allocation_size(size_t size, size_t *total) {
  const size_t overhead = sizeof(heap_allocation_header_t) +
                          KASAN_HEAP_RIGHT_REDZONE + HEAP_ALIGNMENT - 1u;
  if (size == 0 || size > (size_t)INT_MAX - overhead) {
    return false;
  }
  *total = (size + overhead) & ~(HEAP_ALIGNMENT - 1u);
  return true;
}

static void *heap_record_allocation(void *raw, size_t size, size_t total,
                                    uint8_t kasan_type) {
  heap_allocation_header_t *header = raw;
  void *user = header + 1;
  header->size = size;
  header->cookie = (uintptr_t)raw ^ size ^ HEAP_COOKIE;
  kasan_alloc(raw, (uint32_t)total, user, (uint32_t)size, kasan_type);
  return user;
}

static heap_allocation_header_t *heap_allocation_header(void *ptr) {
  if ((uintptr_t)ptr < sizeof(heap_allocation_header_t) ||
      ((uintptr_t)ptr & (HEAP_ALIGNMENT - 1u)) != 0) {
    heap_corruption(ptr);
  }
  heap_allocation_header_t *header =
      (heap_allocation_header_t *)ptr - 1;
  uintptr_t cookie = (uintptr_t)header ^ header->size ^ HEAP_COOKIE;
  if (header->cookie != cookie) {
    heap_corruption(ptr);
  }
  return header;
}

static bool kernel_heap_grow(size_t required) {
  if (required > (size_t)INT_MAX - 2u * HEAP_PAGE_BYTES) {
    return false;
  }
  size_t growth = required + HEAP_PAGE_BYTES;
  if (growth < HEAP_GROWTH_BYTES) {
    growth = HEAP_GROWTH_BYTES;
  }
  growth = (growth + HEAP_PAGE_BYTES - 1u) & ~(HEAP_PAGE_BYTES - 1u);
  if (growth > INT_MAX) {
    return false;
  }
  void *span = page_malloc((int)growth);
  if (span == NULL) {
    return false;
  }
  if (liballoc_heap_extend(span, growth)) {
    return true;
  }
  page_free(span, (int)growth);
  return false;
}

bool kernel_heap_initialize(void) {
  irq_state_t state = irq_save();
  if (heap_ready) {
    irq_restore(state);
    return true;
  }
  void *span = page_malloc(HEAP_INITIAL_BYTES);
  if (span == NULL) {
    irq_restore(state);
    return false;
  }
  if (!liballoc_heap_init(span, HEAP_INITIAL_BYTES)) {
    page_free(span, HEAP_INITIAL_BYTES);
    irq_restore(state);
    return false;
  }
  liballoc_heap_onerror(liballoc_error);
  heap_ready = true;
  irq_restore(state);
  return true;
}

void *malloc(size_t size) {
  size_t total;
  if (!heap_allocation_size(size, &total)) {
    return NULL;
  }

  irq_state_t state = irq_save();
  void *raw = liballoc_aligned_alloc(HEAP_ALIGNMENT, total);
  if (raw == NULL && heap_ready && kernel_heap_grow(total)) {
    raw = liballoc_aligned_alloc(HEAP_ALIGNMENT, total);
  }
  void *user = raw == NULL
                   ? NULL
                   : heap_record_allocation(raw, size, total,
                                            KASAN_ALLOC_MALLOC);
  irq_restore(state);
  return user;
}

void free(void *ptr) {
  if (ptr == NULL) {
    return;
  }
  irq_state_t state = irq_save();
  heap_allocation_header_t *header = heap_allocation_header(ptr);
  kasan_free(ptr, KASAN_ALLOC_MALLOC);
  liballoc_free(header);
  irq_restore(state);
}

void *realloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return malloc(size);
  }
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  irq_state_t state = irq_save();
  size_t old_size = heap_allocation_header(ptr)->size;
  irq_restore(state);
  void *replacement = malloc(size);
  if (replacement == NULL) {
    return NULL;
  }
  memcpy(replacement, ptr, old_size < size ? old_size : size);
  free(ptr);
  return replacement;
}

void *kmalloc(size_t size) {
  size_t total;
  if (!heap_allocation_size(size, &total)) {
    return NULL;
  }
  void *raw = page_malloc((int)total);
  return raw == NULL
             ? NULL
             : heap_record_allocation(raw, size, total, KASAN_ALLOC_KMALLOC);
}

void kfree(void *ptr) {
  if (ptr == NULL) {
    return;
  }
  heap_allocation_header_t *header = heap_allocation_header(ptr);
  size_t total;
  if (!heap_allocation_size(header->size, &total)) {
    heap_corruption(ptr);
  }
  kasan_free(ptr, KASAN_ALLOC_KMALLOC);
  page_free(header, (int)total);
}

void *krealloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return kmalloc(size);
  }
  if (size == 0) {
    kfree(ptr);
    return NULL;
  }
  size_t old_size = heap_allocation_header(ptr)->size;
  void *replacement = kmalloc(size);
  if (replacement == NULL) {
    return NULL;
  }
  memcpy(replacement, ptr, old_size < size ? old_size : size);
  kfree(ptr);
  return replacement;
}
