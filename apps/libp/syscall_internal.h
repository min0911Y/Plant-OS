#ifndef LIBP_SYSCALL_INTERNAL_H
#define LIBP_SYSCALL_INTERNAL_H

#include <ctypes.h>

intptr_t libp_syscall3(uintptr_t number, uintptr_t argument0,
                       uintptr_t argument1, uintptr_t argument2);

#endif
