#ifndef _KASAN_H
#define _KASAN_H

#include <ctypes.h>

#if defined(KERNEL_ARCH_I386)
#include <arch/x86/i386/kasan.h>
#else
#error "KASAN layout is not defined for the selected architecture"
#endif

#ifdef __GNUC__
#define KASAN_NOINSTR __attribute__((no_sanitize_address))
#else
#define KASAN_NOINSTR
#endif

#ifdef KERNEL_KASAN
#define KASAN_ENABLED 1
#define KASAN_HEAP_LEFT_REDZONE 16u
#define KASAN_HEAP_RIGHT_REDZONE 16u
#else
#define KASAN_ENABLED 0
#define KASAN_HEAP_LEFT_REDZONE 0u
#define KASAN_HEAP_RIGHT_REDZONE 0u
#endif
enum kasan_alloc_type {
  KASAN_ALLOC_MALLOC = 1,
  KASAN_ALLOC_KMALLOC = 2,
  KASAN_ALLOC_PAGE = 3,
};

void kasan_init(void) KASAN_NOINSTR;
void kasan_poison(void *addr, uint32_t size, uint8_t tag) KASAN_NOINSTR;
void kasan_unpoison(void *addr, uint32_t size) KASAN_NOINSTR;
void kasan_alloc(void *base, uint32_t total_size, void *user,
                 uint32_t user_size, uint8_t type) KASAN_NOINSTR;
void kasan_free(void *user, uint8_t type) KASAN_NOINSTR;
void kasan_page_alloc(void *base, uint32_t total_size,
                      uint32_t requested_size) KASAN_NOINSTR;
void kasan_page_free(void *base, uint32_t total_size) KASAN_NOINSTR;
int kasan_check_range(const void *addr, uint32_t size,
                      int write) KASAN_NOINSTR;
void kasan_report(const void *addr, uint32_t size, int write,
                  const char *reason) KASAN_NOINSTR;

void __asan_load1_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_load2_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_load4_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_load8_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_load16_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_loadN_noabort(uintptr_t addr, uint32_t size) KASAN_NOINSTR;
void __asan_store1_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_store2_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_store4_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_store8_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_store16_noabort(uintptr_t addr) KASAN_NOINSTR;
void __asan_storeN_noabort(uintptr_t addr, uint32_t size) KASAN_NOINSTR;
void __asan_handle_no_return(void) KASAN_NOINSTR;

#endif
