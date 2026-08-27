#ifndef PLOS_LWIP_ARCH_CC_H
#define PLOS_LWIP_ARCH_CC_H

#include <ctypes.h>
#include <irq.h>
#include <limits.h>
#include <stddef.h>

#define LWIP_NO_STDDEF_H 1
#define LWIP_NO_STDINT_H 1
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_LIMITS_H 1
#define LWIP_NO_UNISTD_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_HAVE_INT64 1

typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
typedef uint64_t u64_t;
typedef int64_t s64_t;
typedef uintptr_t mem_ptr_t;
typedef irq_state_t sys_prot_t;

u32_t lwip_port_rand(void);
void lwip_port_assert(const char *message);

#define LWIP_RAND() lwip_port_rand()
#define LWIP_PLATFORM_DIAG(message) do { } while (0)
#define LWIP_PLATFORM_ASSERT(message) lwip_port_assert(message)

#define SYS_ARCH_DECL_PROTECT(level) sys_prot_t level
#define SYS_ARCH_PROTECT(level) do { (level) = irq_save(); } while (0)
#define SYS_ARCH_UNPROTECT(level) irq_restore(level)

#endif
