#include <stdint.h>
#include <syscall.h>

uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_p) {
  uint64_t quot = 0, qbit = 1;

  if (den == 0) {
    __asm__ __volatile__("int $0");
    return 0; /* If trap returns... */
  }

  /* Left-justify denominator and count shift */
  while ((int64_t)den >= 0) {
    den  <<= 1;
    qbit <<= 1;
  }

  while (qbit) {
    if (den <= num) {
      num  -= den;
      quot += qbit;
    }
    den  >>= 1;
    qbit >>= 1;
  }

  if (rem_p) *rem_p = num;

  return quot;
}
int64_t __divmoddi4(int64_t num, int64_t den, int64_t *rem_p) {
  int64_t quot = 0, qbit = 1;

  if (den == 0) {
    __asm__ __volatile__("int $0");
    return 0; /* If trap returns... */
  }

  /* Left-justify denominator and count shift */
  while ((int64_t)den >= 0) {
    den  <<= 1;
    qbit <<= 1;
  }

  while (qbit) {
    if (den <= num) {
      num  -= den;
      quot += qbit;
    }
    den  >>= 1;
    qbit >>= 1;
  }

  if (rem_p) *rem_p = num;

  return quot;
}

/* $Header$ */

/* replace undef by define */
#undef DEBUG     /* check assertions */
#undef SLOWDEBUG /* some extra test loops (requires DEBUG) */

#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#  define ASSERT(b)                                                                                \
    if (!(b)) assert_failed();
#else
#  define ASSERT(b) /* empty */
#endif

#if WORDSZ == 2
#  define ptrint int
#else
#  define ptrint long
#endif

#if WORDSZ == 2
#  define BRKSIZE 1024
#else
#  define BRKSIZE 4096
#endif
#define PTRSIZE     ((int)sizeof(void *))
#define Align(x, a) (((x) + (a - 1)) & ~(a - 1))
#define NextSlot(p) (*(void **)((p) - PTRSIZE))
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
static void    *_bottom, *_top, *_empty;
unsigned        alloc_start_addr;
static unsigned sz;
static unsigned sz_left = 0;
static unsigned msbrk(unsigned size) {
  unsigned result = alloc_start_addr + sz;
  if (sz_left < size) {
    sbrk((size + 0xfff) & 0xfffff000);
    sz_left += (size + 0xfff) & 0xfffff000;
    sz_left -= size;
  } else {
    sz_left -= size;
  }
  sz += size;
  return result;
}
void abi_alloc_init() {
  alloc_start_addr = api_malloc(1);
  sz               = 0;
  sz_left          = api_heapsize();
  _bottom          = NULL;
  _top             = NULL;
  _empty           = NULL;
}
static int grow(size_t len) {
  register char *p;

  ASSERT(NextSlot((char *)_top) == 0);
  if ((char *)_top + len < (char *)_top ||
      (p = (char *)Align((ptrint)_top + len, BRKSIZE)) < (char *)_top ||
      msbrk(p - (sz + alloc_start_addr)) == 0)
    return (0);
  NextSlot((char *)_top) = p;
  NextSlot(p)            = 0;
  free(_top);
  _top = p;
  return 1;
}

void *malloc(size_t size) {
  register char    *prev, *p, *next, *new;
  register unsigned len, ntries;

  if (size == 0) return NULL;
  for (ntries = 0; ntries < 2; ntries++) {
    if ((len = Align(size, PTRSIZE) + PTRSIZE) < 2 * PTRSIZE) return NULL;
    if (_bottom == 0) {
      if ((p = msbrk(2 * PTRSIZE)) == (char *)-1) return NULL;
      p     = (char *)Align((ptrint)p, PTRSIZE);
      p    += PTRSIZE;
      _top = _bottom = p;
      NextSlot(p)    = 0;
    }
#ifdef SLOWDEBUG
    for (p = _bottom; (next = NextSlot(p)) != 0; p = next)
      ASSERT(next > p);
    ASSERT(p == _top);
#endif
    for (prev = 0, p = _empty; p != 0; prev = p, p = NextFree(p)) {
      next = NextSlot(p);
      new  = p + len;                       /* easily overflows!! */
      if (new > next || new <= p) continue; /* too small */
      if (new + PTRSIZE < next) {           /* too big, so split */
        /* + PTRSIZE avoids tiny slots on free list */
        NextSlot(new) = next;
        NextSlot(p)   = new;
        NextFree(new) = NextFree(p);
        NextFree(p)   = new;
      }
      if (prev)
        NextFree(prev) = NextFree(p);
      else
        _empty = NextFree(p);
      /* clear data */
      //printf("will memset %p\n",memset);
      __builtin_memset(p, 0, size);
      return p;
    }
    if (grow(len) == 0) break;
  }
  ASSERT(ntries != 2);
  return NULL;
}

void *realloc(void *oldp, size_t size) {
  register char  *prev, *p, *next, *new;
  char           *old = oldp;
  register size_t len, n;

  if (!old)
    return malloc(size);
  else if (!size) {
    free(oldp);
    return NULL;
  }
  len  = Align(size, PTRSIZE) + PTRSIZE;
  next = NextSlot(old);
  n    = (int)(next - old); /* old length */
  /*
   * extend old if there is any free space just behind it
   */
  for (prev = 0, p = _empty; p != 0; prev = p, p = NextFree(p)) {
    if (p > next) break;
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
      free(new);
    }
    return old;
  }
  if ((new = malloc(size)) == NULL) /* it didn't fit */
    return NULL;
  memcpy(new, old, n); /* n < size */
  free(old);
  return new;
}

void free(void *ptr) {
  register char *prev, *next;
  char          *p = ptr;

  if (!p) return;

  ASSERT((char *)NextSlot(p) > p);
  for (prev = 0, next = _empty; next != 0; prev = next, next = NextFree(next))
    if (p < next) break;
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

/**
 * Only call malloc, boundary not used, it's not a good idea.
 */
void *memalign(size_t boundary, size_t size) {
  return malloc(size);
}

void *calloc(int num, size_t size) {
  void *p;
  p = malloc(num * size);
  if (p) memset(p, 0, num * size);
  return p;
}