#ifndef _HEAP_H
#define _HEAP_H

#include <ctypes.h>
#include <stddef.h>

bool kernel_heap_initialize(void);
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t size);

#endif
