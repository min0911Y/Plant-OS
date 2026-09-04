#ifndef KERNEL_ARCH_X86_I386_USER_H
#define KERNEL_ARCH_X86_I386_USER_H

#define USER_SPACE_START ((uintptr_t)0x70000000u)
#define USER_HEAP_END ((uintptr_t)0xf0000000u)
#define USER_SHARED_END ((uintptr_t)0xf1000000u)

#endif
