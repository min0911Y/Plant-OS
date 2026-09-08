#include "runtime_lifecycle.h"
#include <errno.h>
#include <futex.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall.h>

uintptr_t api_malloc(int size);

uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_p) {
  uint64_t quot = 0, qbit = 1;

  if (den == 0) {
    __builtin_trap();
    return 0; /* If trap returns... */
  }

  /* Left-justify denominator and count shift */
  while ((int64_t)den >= 0) {
    den <<= 1;
    qbit <<= 1;
  }

  while (qbit) {
    if (den <= num) {
      num -= den;
      quot += qbit;
    }
    den >>= 1;
    qbit >>= 1;
  }

  if (rem_p)
    *rem_p = num;

  return quot;
}
int64_t __divmoddi4(int64_t num, int64_t den, int64_t *rem_p) {
  uint64_t numerator = num < 0 ? 0 - (uint64_t)num : (uint64_t)num;
  uint64_t denominator = den < 0 ? 0 - (uint64_t)den : (uint64_t)den;
  uint64_t remainder;
  uint64_t quotient = __udivmoddi4(numerator, denominator, &remainder);
  if (rem_p)
    *rem_p = num < 0 ? 0 - remainder : remainder;
  return (num < 0) != (den < 0) ? 0 - quotient : quotient;
}

/* $Header$ */

/* replace undef by define */
#undef DEBUG     /* check assertions */
#undef SLOWDEBUG /* some extra test loops (requires DEBUG) */

#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#define ASSERT(b)                                                              \
  if (!(b))                                                                    \
    assert_failed();
#else
#define ASSERT(b) /* empty */
#endif

#if WORDSZ == 2
#define ptrint int
#else
#define ptrint uintptr_t
#endif

#if WORDSZ == 2
#define BRKSIZE 1024
#else
#define BRKSIZE 4096
#endif
#define PTRSIZE ((int)(sizeof(void *) == 8 ? 16 : _Alignof(max_align_t)))
#define Align(x, a) (((x) + (a - 1)) & ~(a - 1))
#define NextSlot(p) (*(void **)((p)-PTRSIZE))
#define NextFree(p) (*(void **)(p))

#ifdef DEBUG
static void assert_failed() {
  write(2, "assert failed in lib/malloc.c\n", 30);
  abort();
}
#endif

/*
 * A short explanation of the data structure and algorithms.
 * An area returned by malloc() is called a slot. Each slot
 * contains the number of bytes requested, but preceeded by
 * an extra pointer to the next the slot in memory.
 * '_bottom' and '_top' point to the first/last slot.
 * More memory is asked for using brk() and appended to top.
 * The list of free slots is maintained to keep malloc() fast.
 * '_empty' points the the first free slot. Free slots are
 * linked together by a pointer at the start of the
 * user visable part, so just after the next-slot pointer.
 * Free slots are merged together by free().
 */
static void *_bottom, *_top, *_empty;
uintptr_t alloc_start_addr;
static size_t sz;
static size_t sz_left;
static uintptr_t dirty_end;
static uint32_t allocator_lock;

void allocator_lock_acquire(void) {
  uint32_t expected = 0;
  if (__atomic_compare_exchange_n(&allocator_lock, &expected, 1, false,
                                  __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    return;
  while (__atomic_exchange_n(&allocator_lock, 2, __ATOMIC_ACQUIRE))
    os_futex_wait(&allocator_lock, 2, UINT64_MAX);
}

void allocator_lock_release(void) {
  if (__atomic_exchange_n(&allocator_lock, 0, __ATOMIC_RELEASE) == 2)
    os_futex_wake(&allocator_lock, 1);
}

static void free_unlocked(void *ptr);
static void *malloc_unlocked(size_t size);

static uintptr_t msbrk(size_t size) {
  if (sz > UINTPTR_MAX - alloc_start_addr)
    return (uintptr_t)-1;
  uintptr_t result = alloc_start_addr + sz;
  if (sz_left < size) {
    size_t missing = size - sz_left;
    size_t request = (missing + 0xfff) & ~(size_t)0xfff;
    if (request < missing || sbrk(request) < 0 || sz_left > SIZE_MAX - request)
      return (uintptr_t)-1;
    sz_left += request;
  }
  if (sz_left < size || sz > SIZE_MAX - size)
    return (uintptr_t)-1;
  sz_left -= size;
  sz += size;
  return result;
}
void abi_alloc_init() {
  alloc_start_addr = api_malloc(1);
  sz = 0;
  sz_left = api_heapsize();
  dirty_end = alloc_start_addr;
  _bottom = NULL;
  _top = NULL;
  _empty = NULL;
  allocator_lock = 0;
}
static int grow(size_t len) {
  register char *p;
  uintptr_t top = (uintptr_t)_top;
  uintptr_t end;
  uintptr_t current_end;
  uintptr_t aligned_end;

  ASSERT(NextSlot((char *)_top) == 0);
  if (len > UINTPTR_MAX - top)
    return (0);
  end = top + len;
  aligned_end = Align(end, BRKSIZE);
  if (aligned_end < end || aligned_end < top ||
      sz > UINTPTR_MAX - alloc_start_addr)
    return (0);
  current_end = alloc_start_addr + sz;
  if (aligned_end < current_end ||
      msbrk((size_t)(aligned_end - current_end)) == (uintptr_t)-1)
    return (0);
  p = (char *)aligned_end;
  NextSlot((char *)_top) = p;
  NextSlot(p) = 0;
  free_unlocked(_top);
  _top = p;
  return 1;
}

static void *malloc_unlocked(size_t size) {
  register char *prev, *p, *next, *new;
  register size_t len;
  register unsigned ntries;

  if (size == 0 || size > SIZE_MAX - (2 * PTRSIZE - 1))
    return NULL;
  for (ntries = 0; ntries < 2; ntries++) {
    if ((len = Align(size, PTRSIZE) + PTRSIZE) < 2 * PTRSIZE)
      return NULL;
    if (_bottom == 0) {
      if ((p = (char *)msbrk(2 * PTRSIZE)) == (char *)-1)
        return NULL;
      p = (char *)Align((ptrint)p, PTRSIZE);
      p += PTRSIZE;
      _top = _bottom = p;
      NextSlot(p) = 0;
    }
#ifdef SLOWDEBUG
    for (p = _bottom; (next = NextSlot(p)) != 0; p = next)
      ASSERT(next > p);
    ASSERT(p == _top);
#endif
    for (prev = 0, p = _empty; p != 0; prev = p, p = NextFree(p)) {
      next = NextSlot(p);
      new = p + len; /* easily overflows!! */
      if (new > next || new <= p)
        continue;                 /* too small */
      if (new + PTRSIZE < next) { /* too big, so split */
        /* + PTRSIZE avoids tiny slots on free list */
        NextSlot(new) = next;
        NextSlot(p) = new;
        NextFree(new) = NextFree(p);
        NextFree(p) = new;
      }
      if (prev)
        NextFree(prev) = NextFree(p);
      else
        _empty = NextFree(p);
      // Fresh heap pages already read as zero. Clear reused storage and the
      // free-list link, without materializing untouched copy-on-write pages.
      size_t clear = (uintptr_t)p < dirty_end ? dirty_end - (uintptr_t)p : 0;
      if (clear < sizeof(void *))
        clear = sizeof(void *);
      __builtin_memset(p, 0, clear < size ? clear : size);
      uintptr_t end = (uintptr_t) new + PTRSIZE;
      if (end > dirty_end)
        dirty_end = end;
      return p;
    }
    if (grow(len) == 0)
      break;
  }
  ASSERT(ntries != 2);
  return NULL;
}

void *malloc(size_t size) {
  void *result;
  allocator_lock_acquire();
  result = malloc_unlocked(size);
  allocator_lock_release();
  if (!result && size)
    errno = ENOMEM;
  return result;
}

static void *realloc_unlocked(void *oldp, size_t size) {
  register char *prev, *p, *next, *new;
  char *old = oldp;
  register size_t len, n;

  if (!old)
    return malloc_unlocked(size);
  else if (!size) {
    free_unlocked(oldp);
    return NULL;
  }
  len = Align(size, PTRSIZE) + PTRSIZE;
  next = NextSlot(old);
  n = (size_t)(next - old); /* old length */
  /*
   * extend old if there is any free space just behind it
   */
  for (prev = 0, p = _empty; p != 0; prev = p, p = NextFree(p)) {
    if (p > next)
      break;
    if (p == next) { /* 'next' is a free slot: merge */
      NextSlot(old) = NextSlot(p);
      if (prev)
        NextFree(prev) = NextFree(p);
      else
        _empty = NextFree(p);
      next = NextSlot(old);
      break;
    }
  }
  new = old + len;
  /*
   * Can we use the old, possibly extended slot?
   */
  if (new <= next &&new >= old) { /* it does fit */
    if (new + PTRSIZE < next) {   /* too big, so split */
      /* + PTRSIZE avoids tiny slots on free list */
      NextSlot(new) = next;
      NextSlot(old) = new;
      free_unlocked(new);
    }
    uintptr_t end = (uintptr_t) new + PTRSIZE;
    if (end > dirty_end)
      dirty_end = end;
    return old;
  }
  if ((new = malloc_unlocked(size)) == NULL) /* it didn't fit */
    return NULL;
  memcpy(new, old, n); /* n < size */
  free_unlocked(old);
  return new;
}

void *realloc(void *oldp, size_t size) {
  void *result;
  allocator_lock_acquire();
  result = realloc_unlocked(oldp, size);
  allocator_lock_release();
  return result;
}

static void free_unlocked(void *ptr) {
  register char *prev, *next;
  char *p = ptr;

  if (!p)
    return;

  uintptr_t end = (uintptr_t)p + PTRSIZE;
  if (end > dirty_end)
    dirty_end = end;

  ASSERT((char *)NextSlot(p) > p);
  for (prev = 0, next = _empty; next != 0; prev = next, next = NextFree(next))
    if (p < next)
      break;
  NextFree(p) = next;
  if (prev)
    NextFree(prev) = p;
  else
    _empty = p;
  if (next) {
    ASSERT((char *)NextSlot(p) <= next);
    if (NextSlot(p) == next) { /* merge p and next */
      NextSlot(p) = NextSlot(next);
      NextFree(p) = NextFree(next);
    }
  }
  if (prev) {
    ASSERT((char *)NextSlot(prev) <= p);
    if (NextSlot(prev) == p) { /* merge prev and p */
      NextSlot(prev) = NextSlot(p);
      NextFree(prev) = NextFree(p);
    }
  }
}

void free(void *ptr) {
  if (!ptr)
    return;
  allocator_lock_acquire();
  free_unlocked(ptr);
  allocator_lock_release();
}

static void *aligned_unlocked(size_t alignment, size_t size) {
  if (alignment <= PTRSIZE)
    return malloc_unlocked(size ? size : 1);
  if (alignment > SIZE_MAX - 2 * PTRSIZE ||
      size > SIZE_MAX - alignment - 2 * PTRSIZE)
    return NULL;
  char *base = malloc_unlocked(size + alignment + 2 * PTRSIZE);
  if (!base)
    return NULL;
  char *aligned = (void *)Align((uintptr_t)base, alignment);
  if (aligned != base && aligned - base < 2 * PTRSIZE)
    aligned += alignment;
  if (aligned != base) {
    NextSlot(aligned) = NextSlot(base);
    NextSlot(base) = aligned;
    free_unlocked(base);
  }
  size_t used = Align(size ? size : 1, PTRSIZE) + PTRSIZE;
  char *end = NextSlot(aligned);
  if ((size_t)(end - aligned) - used >= 2 * PTRSIZE) {
    char *tail = aligned + used;
    NextSlot(tail) = end;
    NextSlot(aligned) = tail;
    free_unlocked(tail);
  }
  return aligned;
}

int posix_memalign(void **pointer, size_t alignment, size_t size) {
  if (!pointer || alignment < sizeof(void *) || (alignment & (alignment - 1)))
    return EINVAL;
  allocator_lock_acquire();
  void *result = aligned_unlocked(alignment, size);
  allocator_lock_release();
  if (!result)
    return ENOMEM;
  *pointer = result;
  return 0;
}

void *memalign(size_t alignment, size_t size) {
  if (!alignment || (alignment & (alignment - 1))) {
    errno = EINVAL;
    return NULL;
  }
  void *result = NULL;
  int error = posix_memalign(
      &result, alignment < sizeof(void *) ? sizeof(void *) : alignment, size);
  if (error)
    errno = error;
  return result;
}
void *aligned_alloc(size_t alignment, size_t size) {
  if (!alignment || (alignment & (alignment - 1)) || size % alignment) {
    errno = EINVAL;
    return NULL;
  }
  return memalign(alignment, size);
}

void *calloc(size_t num, size_t size) {
  if (num && size > SIZE_MAX / num) {
    errno = ENOMEM;
    return NULL;
  }
  void *p;
  p = malloc(num * size);
  if (p)
    memset(p, 0, num * size);
  return p;
}
