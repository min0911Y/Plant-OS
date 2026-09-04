#ifndef KERNEL_ARCH_X86_IO_H
#define KERNEL_ARCH_X86_IO_H

#include <ctypes.h>

static inline uint8_t x86_port_read8(uint16_t port) {
  uint8_t value;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
  return value;
}

static inline uint16_t x86_port_read16(uint16_t port) {
  uint16_t value;
  asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
  return value;
}

static inline uint32_t x86_port_read32(uint16_t port) {
  uint32_t value;
  asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
  return value;
}

static inline void x86_port_write8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline void x86_port_write16(uint16_t port, uint16_t value) {
  asm volatile("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline void x86_port_write32(uint16_t port, uint32_t value) {
  asm volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline void x86_port_read32s(uint16_t port, void *buffer,
                                    size_t count) {
  asm volatile("cld; rep insl"
               : "+D"(buffer), "+c"(count)
               : "d"(port)
               : "memory");
}

static inline void x86_port_write16s(uint16_t port, const void *buffer,
                                     size_t count) {
  asm volatile("cld; rep outsw"
               : "+S"(buffer), "+c"(count)
               : "d"(port)
               : "memory");
}

static inline void x86_io_wait(void) {
  x86_port_write8(0x80, 0);
}

#endif
