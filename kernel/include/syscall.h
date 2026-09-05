#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stddef.h>

typedef uintptr_t syscall_word_t;
enum {
  SYSCALL_ARCH_RESET_FPU = 0x2f,
  SYSCALL_ARCH_SIGNAL_RETURN = 0x65,
};

typedef struct {
  syscall_word_t value;
  syscall_word_t argument0;
  syscall_word_t argument1;
  syscall_word_t argument2;
  syscall_word_t argument3;
  syscall_word_t argument4;
  syscall_word_t argument5;
} syscall_context_t;

void syscall_dispatch(syscall_context_t *context);

#endif
