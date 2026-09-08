#ifndef __STDDEF__
#define __STDDEF__
#include <ctypes.h>
#ifndef NULL
#define NULL ((void*)0)
#endif
#define offsetof(type, member) __builtin_offsetof(type, member)
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __SIZE_TYPE__ size_t;
#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif
typedef struct {
  long long integer __attribute__((aligned(__alignof__(long long))));
  long double floating __attribute__((aligned(__alignof__(long double))));
} max_align_t;
typedef int errno_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#endif
