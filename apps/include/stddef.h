#ifndef __STDDEF__
#define __STDDEF__
#include <sys/types.h>
#ifndef NULL
#define NULL ((void*)0)
#endif
#define offsetof(s,m) (size_t)&(((s *)0)->m)
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __SIZE_TYPE__ size_t;
#ifndef __cplusplus
#endif
typedef int errno_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#endif
