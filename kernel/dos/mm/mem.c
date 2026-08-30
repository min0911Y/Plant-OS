#include <arch/x86/control.h>
#include <dos.h>
#include <irq.h>

/* 写入 pattern、两次取反回读，最后恢复原值；返回该 dword 是否为真实内存。 */
static bool memory_probe_dword(volatile uint32_t *probe,
                               uintptr_t preserved_start,
                               uint32_t preserved_size) {
  uintptr_t address = (uintptr_t)probe;
  if (preserved_size != 0 && address >= preserved_start &&
      address - preserved_start < preserved_size) {
    return true;
  }

  const uint32_t pattern = 0xaa55aa55u;
  uint32_t saved = *probe;

  *probe = pattern;
  *probe ^= 0xffffffffu;
  bool usable = *probe == ~pattern;
  if (usable) {
    *probe ^= 0xffffffffu;
    usable = *probe == pattern;
  }
  *probe = saved;
  return usable;
}

/* 从 1GiB 的步长开始向上探测，失败就把步长缩到 1/4，最小 4KiB，
 * 返回第一个不可用地址，即可用内存的上界。 */
static unsigned int memory_probe_limit(unsigned int start, unsigned int end,
                                       uintptr_t preserved_start,
                                       uint32_t preserved_size) {
  unsigned int address = start;

  for (unsigned int block = 1024u * 1024u * 1024u; block >= 0x1000u;) {
    volatile uint32_t *probe =
        (volatile uint32_t *)(address + block - sizeof(uint32_t));
    if (!memory_probe_dword(probe, preserved_start, preserved_size)) {
      block /= 4;
      continue;
    }
    address += block;
    if (address > end) {
      break;
    }
  }
  return address;
}

unsigned int memtest(unsigned int start, unsigned int end,
                     uintptr_t preserved_start, uint32_t preserved_size) {
  /* 386 无法把 AC 位置 1，只有 486 及以上才支持并需要临时关闭缓存。 */
  uint32_t eflags = x86_eflags_read();
  x86_eflags_write(eflags | X86_EFLAGS_AC);
  bool cache_control = (x86_eflags_read() & X86_EFLAGS_AC) != 0;
  x86_eflags_write(eflags & ~X86_EFLAGS_AC);

  irq_state_t state = irq_save();
  if (cache_control) {
    x86_cr0_write(x86_cr0_read() | X86_CR0_CD | X86_CR0_NW);
  }
  unsigned int limit =
      memory_probe_limit(start, end, preserved_start, preserved_size);
  if (cache_control) {
    x86_cr0_write(x86_cr0_read() & ~(X86_CR0_CD | X86_CR0_NW));
  }
  irq_restore(state);
  return limit;
}
