#include <arch/x86/x86_64/cpu.h>
#include <arch/x86/interrupt_controller.h>
#include <dos.h>
#include <irq.h>
#include <page_fault.h>
#include <syscall.h>
#include <user_space.h>

enum { KERNEL_CODE = 8, KERNEL_DATA = 16, USER_DATA = 27, USER_CODE = 35 };
typedef struct {
  uint32_t reserved0;
  uint64_t rsp[3];
  uint64_t reserved1;
  uint64_t ist[7];
  uint64_t reserved2;
  uint16_t reserved3, iomap;
} __attribute__((packed)) task_state_t;
typedef struct {
  uint16_t low, selector;
  uint8_t ist, attributes;
  uint16_t middle;
  uint32_t high, reserved;
} __attribute__((packed)) idt_entry_t;
typedef struct {
  uint16_t limit;
  uintptr_t base;
} __attribute__((packed)) descriptor_pointer_t;

static uint64_t gdt[SMP_MAX_CPUS][7] __attribute__((aligned(16)));
static task_state_t tss[SMP_MAX_CPUS];
static uint8_t emergency_stacks[SMP_MAX_CPUS][3][16384]
    __attribute__((aligned(16)));
static idt_entry_t idt[256] __attribute__((aligned(16)));
extern void *x64_interrupt_entries[256];
extern void x64_syscall_entry(void);

static void descriptor_load(void) {
  uint32_t cpu = smp_current_cpu();
  task_state_t *state = &tss[cpu];
  memset(state, 0, sizeof(*state));
  for (size_t i = 0; i < 3; i++)
    state->ist[i] = (uintptr_t)&emergency_stacks[cpu][i][16384];
  state->iomap = sizeof(*state);
  uintptr_t base = (uintptr_t)state;
  uint64_t *table = gdt[cpu];
  table[0] = 0;
  table[1] = 0x00af9a000000ffffull;
  table[2] = 0x00cf92000000ffffull;
  table[3] = 0x00cff2000000ffffull;
  table[4] = 0x00affa000000ffffull;
  table[5] = (sizeof(*state) - 1) | ((base & 0xffffff) << 16) |
             (0x89ull << 40) | ((base & 0xff000000) << 32);
  table[6] = base >> 32;
  descriptor_pointer_t gdtr = {sizeof(gdt[cpu]) - 1, (uintptr_t)table};
  descriptor_pointer_t idtr = {sizeof(idt) - 1, (uintptr_t)idt};
  __asm__ volatile(
      "lgdt %0; pushq $8; leaq 1f(%%rip), %%rax; pushq %%rax; lretq; 1:"
      :
      : "m"(gdtr)
      : "rax", "memory");
  __asm__ volatile("movw %0, %%ds; movw %0, %%es; movw %0, %%ss"
                   :
                   : "r"((uint16_t)KERNEL_DATA)
                   : "memory");
  __asm__ volatile("ltr %0; lidt %1"
                   :
                   : "r"((uint16_t)40), "m"(idtr)
                   : "memory");
}

void arch_interrupt_init(void) {
  for (size_t i = 0; i < 256; i++) {
    uintptr_t entry = (uintptr_t)x64_interrupt_entries[i];
    uint8_t ist = i == 2 ? 1 : i == 8 ? 2 : i == 18 ? 3 : 0;
    idt[i] = (idt_entry_t){entry,       KERNEL_CODE, ist, 0x8e,
                           entry >> 16, entry >> 32, 0};
  }
  descriptor_load();
}
void arch_interrupt_init_secondary(void) { descriptor_load(); }
void arch_task_state_init(void) {
  /* The TSS is installed together with this CPU's GDT, before interrupts. */
  tss[smp_current_cpu()].iomap = sizeof(task_state_t);
}
void arch_task_set_kernel_stack(uintptr_t top) {
  tss[smp_current_cpu()].rsp[0] = top;
  x64_this_cpu()->kernel_stack = top;
}
void x64_syscall_initialize(void) {
  x64_msr_write(0xc0000080, x64_msr_read(0xc0000080) | 1);
  x64_msr_write(0xc0000081, ((uint64_t)(USER_CODE - 16) << 48) |
                                ((uint64_t)KERNEL_CODE << 32));
  x64_msr_write(0xc0000082, (uintptr_t)x64_syscall_entry);
  /* Clear IF, TF, DF, NT and AC before touching a kernel stack. */
  x64_msr_write(0xc0000084,
                (1u << 9) | (1u << 8) | (1u << 10) | (1u << 14) | (1u << 18));
}

void x64_interrupt_dispatch(x64_interrupt_frame_t *frame) {
  unsigned vector = frame->vector & 255;
  if (vector == 2 || vector == 8 || vector == 18) {
    logk("fatal x86_64 exception %u rip=%llx error=%llx\n", vector, frame->rip,
         frame->error);
    arch_halt();
  }
  if (vector == 0xff)
    return;
  if (vector == 0xf1) {
    apic_send_eoi();
    return;
  }
  if (vector == X64_TLB_VECTOR) {
    x64_tlb_poll();
    apic_send_eoi();
    return;
  }
  kernel_lock_enter();
  if (vector < 32) {
    uintptr_t address = 0;
    if (vector == 14)
      __asm__ volatile("mov %%cr2, %0" : "=r"(address));
    if (vector == 14 && page_fault_try_resolve(address, frame->error)) {
      kernel_lock_leave();
      return;
    }
    logk(
        "x86_64 exception %u tid=%u rip=%llx address=%llx error=%llx cs=%llx\n",
        vector, current_task()->tid, frame->rip, address, frame->error,
        frame->cs);
    logk("registers rax=%llx rbx=%llx rcx=%llx rdx=%llx rsp=%llx\n", frame->rax,
         frame->rbx, frame->rcx, frame->rdx, frame->rsp);
    logk("registers rsi=%llx rdi=%llx rbp=%llx\n", frame->rsi, frame->rdi,
         frame->rbp);
    if ((frame->cs & 3) == 3)
      task_exit((unsigned)-1);
    arch_halt();
  }
  if (vector == 0xf0)
    scheduler_reschedule_interrupt();
  else if (vector >= 0x20 && vector < 0x38) {
    irq_dispatch(vector - 0x20);
    if (vector <= 0x21)
      signal_deal();
  } else if (vector >= X86_MESSAGE_VECTOR_FIRST &&
             vector < X86_MESSAGE_VECTOR_END) {
    irq_dispatch(vector);
  }
  kernel_lock_leave();
}

void x64_syscall_dispatch(x64_interrupt_frame_t *frame) {
  kernel_lock_enter();
  if (current_task()->terminate_pending)
    task_exit(current_task()->terminate_status);
  if (frame->rax == SYSCALL_ARCH_SIGNAL_RETURN) {
    if (!x64_user_access(frame->rdi, sizeof(*frame), false))
      task_exit(141);
    x64_interrupt_frame_t saved = *(const x64_interrupt_frame_t *)frame->rdi;
    uint32_t mxcsr = saved.simd.mxcsr;
    if (saved.rip < USER_SPACE_START || saved.rip >= USER_SPACE_END ||
        saved.rsp < USER_SPACE_START || saved.rsp >= USER_SPACE_END ||
        (mxcsr & ~x64_mxcsr_mask))
      task_exit(141);
    saved.cs = USER_CODE;
    saved.ss = USER_DATA;
    saved.rflags = (saved.rflags & 0x240dd5) | 0x202;
    saved.vector = 0;
    *frame = saved;
    kernel_lock_leave();
    return;
  }
  bool reset_simd = frame->rax == SYSCALL_ARCH_RESET_FPU;
  syscall_context_t call = {frame->rax, frame->rdi, frame->rsi, frame->rdx,
                            frame->r10, frame->r8,  frame->r9};
  irq_enable();
  syscall_dispatch(&call);
  if (reset_simd)
    frame->simd = current_task()->fpu_state.legacy;
  frame->rax = call.value;
  frame->rdi = call.argument0;
  frame->rsi = call.argument1;
  frame->rdx = call.argument2;
  frame->r10 = call.argument3;
  frame->r8 = call.argument4;
  frame->r9 = call.argument5;
  signal_deal();
  if (frame->rip < USER_SPACE_START || frame->rip >= USER_SPACE_END ||
      frame->rsp < USER_SPACE_START || frame->rsp >= USER_SPACE_END)
    task_exit(141);
  frame->rflags = (frame->rflags & 0x240dd5) | 0x202;
  kernel_lock_leave();
}
