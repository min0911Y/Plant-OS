#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <ctypes.h>

enum { SMP_MAX_CPUS = 32 };

void smp_topology_init(void);
void smp_start_aps(void);
void smp_request_secondary_release(void);
uint32_t smp_cpu_count(void);
uint32_t smp_online_cpu_count(void);
uint32_t smp_current_cpu(void);
uint32_t smp_cpu_lapic_id(uint32_t cpu);
int smp_cpu_online(uint32_t cpu);
void smp_send_reschedule(uint32_t cpu);

void kernel_lock_enter(void);
void kernel_lock_leave(void);
uint32_t kernel_lock_depth(void);

#endif
