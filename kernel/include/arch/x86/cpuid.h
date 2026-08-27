#ifndef KERNEL_ARCH_X86_CPUID_H
#define KERNEL_ARCH_X86_CPUID_H

#include <ctypes.h>

/* 字段顺序即 CPUID 的寄存器返回顺序，可直接按小端字节流复制。 */
typedef struct {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
} x86_cpuid_t;

/* CPUID 会覆盖全部四个通用寄存器，EBX 必须作为输出约束交给编译器保存。 */
static inline x86_cpuid_t x86_cpuid(uint32_t leaf, uint32_t subleaf) {
  x86_cpuid_t regs;
  asm volatile("cpuid"
               : "=a"(regs.eax), "=b"(regs.ebx), "=c"(regs.ecx),
                 "=d"(regs.edx)
               : "a"(leaf), "c"(subleaf));
  return regs;
}

#endif
