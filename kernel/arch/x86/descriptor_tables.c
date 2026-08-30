#include <arch.h>
#include <arch/x86/bios.h>
#include <arch/x86/control.h>
#include <arch/x86/interrupt.h>
#include <dos.h>
#include <interrupts.h>
#include <irq.h>
#include <smp.h>

#define X86_IDT_ADDRESS ((uintptr_t)0x0026f800u)
#define X86_GDT_ADDRESS ((uintptr_t)0x00270000u)
#define X86_IDT_LIMIT 0x07ffu
#define X86_GDT_LIMIT 0xffffu

enum {
  X86_IDT_ENTRY_COUNT = 256,
  X86_VECTOR_APIC_SPURIOUS = 0xff,
  X86_GDT_ENTRY_COUNT = (X86_GDT_LIMIT + 1) / 8,
  X86_GDT_KERNEL_DATA_INDEX = 1,
  X86_GDT_KERNEL_CODE_INDEX = 2,
  X86_GDT_USER_DATA_INDEX = 3,
  X86_GDT_USER_CODE_INDEX = 4,
  X86_GDT_USER_BASE_INDEX = 5,
  X86_GDT_TSS_INDEX = 103,
  X86_GDT_BIOS_CODE32_INDEX = 1000,
  X86_GDT_BIOS_CODE16_INDEX = 1001,
  X86_GDT_BIOS_DATA16_INDEX = 1002,
  X86_PRIVILEGE_USER = 3,
  X86_ACCESS_DATA16_RW = 0x0092,
  X86_ACCESS_DATA32_RW = 0x4092,
  X86_ACCESS_CODE16_ER = 0x009a,
  X86_ACCESS_CODE32_ER = 0x409a,
  X86_ACCESS_INTERRUPT_GATE = 0x008e,
  X86_ACCESS_TSS32 = 0x0089,
};

#define X86_SELECTOR(index) ((uint16_t)((index) * 8u))
#define X86_USER_SELECTOR(index)                                               \
  ((uint16_t)(X86_SELECTOR(index) | X86_PRIVILEGE_USER))
#define X86_USER_ACCESS(access)                                                \
  ((uint16_t)((access) | (X86_PRIVILEGE_USER << 5)))

typedef struct {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t limit_high;
  uint8_t base_high;
} __attribute__((packed)) x86_segment_descriptor_t;

typedef struct {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t zero;
  uint8_t access;
  uint16_t offset_high;
} __attribute__((packed)) x86_gate_descriptor_t;

typedef struct {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed)) x86_descriptor_table_pointer_t;

typedef struct {
  uint32_t backlink;
  uint32_t esp0;
  uint32_t ss0;
  uint32_t esp1;
  uint32_t ss1;
  uint32_t esp2;
  uint32_t ss2;
  uint32_t cr3;
  uint32_t eip;
  uint32_t eflags;
  uint32_t eax;
  uint32_t ecx;
  uint32_t edx;
  uint32_t ebx;
  uint32_t esp;
  uint32_t ebp;
  uint32_t esi;
  uint32_t edi;
  uint32_t es;
  uint32_t cs;
  uint32_t ss;
  uint32_t ds;
  uint32_t fs;
  uint32_t gs;
  uint32_t ldtr;
  uint32_t iomap;
} __attribute__((packed)) x86_tss32_t;

_Static_assert(sizeof(x86_segment_descriptor_t) == 8,
               "x86 segment descriptor size");
_Static_assert(sizeof(x86_gate_descriptor_t) == 8,
               "x86 gate descriptor size");
_Static_assert(sizeof(x86_descriptor_table_pointer_t) == 6,
               "x86 descriptor table pointer size");
_Static_assert(sizeof(x86_tss32_t) == 104, "x86 task-state size");
_Static_assert(sizeof(regs16_t) == 26, "x86 BIOS register frame size");
_Static_assert(X86_VECTOR_APIC_SPURIOUS + 1 == X86_IDT_ENTRY_COUNT,
               "x86 IDT includes the APIC spurious vector");

static x86_tss32_t task_states[SMP_MAX_CPUS];
extern unsigned char *IVT;
void x86_bios_interrupt_raw(uint8_t interrupt_number, regs16_t *registers);

static x86_segment_descriptor_t *x86_gdt(void) {
  return (x86_segment_descriptor_t *)X86_GDT_ADDRESS;
}

static x86_gate_descriptor_t *x86_idt(void) {
  return (x86_gate_descriptor_t *)X86_IDT_ADDRESS;
}

static void x86_segment_descriptor_set(x86_segment_descriptor_t *descriptor,
                                       uint32_t limit, uintptr_t base,
                                       uint16_t access) {
  if (limit > 0xfffffu) {
    access |= 0x8000u;
    limit /= 0x1000u;
  }
  descriptor->limit_low = limit & 0xffffu;
  descriptor->base_low = base & 0xffffu;
  descriptor->base_mid = (base >> 16) & 0xffu;
  descriptor->access = access & 0xffu;
  descriptor->limit_high =
      ((limit >> 16) & 0x0fu) | ((access >> 8) & 0xf0u);
  descriptor->base_high = (base >> 24) & 0xffu;
}

static void x86_gate_descriptor_set(x86_gate_descriptor_t *descriptor,
                                    interrupt_entry_t entry,
                                    uint16_t access) {
  uintptr_t offset = (uintptr_t)entry;
  descriptor->offset_low = offset & 0xffffu;
  descriptor->selector = X86_SELECTOR(X86_GDT_KERNEL_CODE_INDEX);
  descriptor->zero = 0;
  descriptor->access = access & 0xffu;
  descriptor->offset_high = (offset >> 16) & 0xffffu;
}

static inline void
x86_gdt_load(const x86_descriptor_table_pointer_t *table) {
  asm volatile("lgdt %0" : : "m"(*table) : "memory");
}

static inline void
x86_idt_load(const x86_descriptor_table_pointer_t *table) {
  asm volatile("lidt %0" : : "m"(*table) : "memory");
}

static inline void x86_task_register_load(uint16_t selector) {
  asm volatile("ltr %0" : : "m"(selector) : "memory");
}

/* lgdt 之后段寄存器里仍是旧表的缓存描述符，必须重新载入内核数据段。 */
static inline void x86_data_segments_load(uint16_t selector) {
  asm volatile("movw %0, %%ds\n"
               "movw %0, %%es\n"
               "movw %0, %%fs\n"
               "movw %0, %%gs"
               :
               : "rm"(selector)
               : "memory");
}

/* 内核自身的控制台尚未初始化，启动期失败只能直写文本 VRAM 后停机。 */
__attribute__((noreturn)) static void x86_boot_fail(const char *message) {
  volatile uint16_t *cell = (volatile uint16_t *)0xb8000u;
  for (const char *c = message; *c != '\0'; c++) {
    *cell++ = (uint16_t)((uint8_t)*c) | 0x4f00u;
  }
  for (;;) {
    asm volatile("cli; hlt");
  }
}

void arch_boot_verify(void) {
  uint16_t cs;
  asm volatile("movw %%cs, %0" : "=rm"(cs));
  if (cs != X86_SELECTOR(X86_GDT_KERNEL_CODE_INDEX)) {
    x86_boot_fail("plant os: kernel entered with an unexpected CS selector");
  }
}

static void x86_interrupt_entry_set(unsigned vector, interrupt_entry_t entry,
                                    uint16_t access) {
  x86_gate_descriptor_set(&x86_idt()[vector], entry, access);
}

void arch_interrupt_init(void) {
  irq_state_t state = irq_save();
  x86_segment_descriptor_t *gdt = x86_gdt();
  for (unsigned i = 0; i < X86_GDT_ENTRY_COUNT; i++) {
    x86_segment_descriptor_set(&gdt[i], 0, 0, 0);
  }
  x86_segment_descriptor_set(&gdt[X86_GDT_KERNEL_DATA_INDEX], 0xffffffffu, 0,
                             X86_ACCESS_DATA32_RW);
  x86_segment_descriptor_set(&gdt[X86_GDT_KERNEL_CODE_INDEX], 0xffffffffu, 0,
                             X86_ACCESS_CODE32_ER);
  x86_segment_descriptor_set(
      &gdt[X86_GDT_USER_DATA_INDEX], 0xffffffffu, 0,
      X86_USER_ACCESS(X86_ACCESS_DATA32_RW));
  x86_segment_descriptor_set(
      &gdt[X86_GDT_USER_CODE_INDEX], 0xffffffffu, 0,
      X86_USER_ACCESS(X86_ACCESS_CODE32_ER));
  x86_segment_descriptor_set(
      &gdt[X86_GDT_USER_BASE_INDEX], 0xffffffffu, 0x70000000u,
      X86_USER_ACCESS(X86_ACCESS_DATA32_RW));

  x86_descriptor_table_pointer_t gdt_pointer = {
      .limit = X86_GDT_LIMIT,
      .base = X86_GDT_ADDRESS,
  };
  x86_gdt_load(&gdt_pointer);
  x86_data_segments_load(X86_SELECTOR(X86_GDT_KERNEL_DATA_INDEX));

  /* null_inthandler only iretds. This includes the APIC spurious vector and
   * deliberately sends no EOI. */
  for (unsigned vector = 0; vector < X86_IDT_ENTRY_COUNT; vector++) {
    x86_interrupt_entry_set(vector, null_inthandler,
                            X86_ACCESS_INTERRUPT_GATE);
  }

  for (unsigned vector = 0; vector < X86_EXCEPTION_COUNT; vector++) {
    x86_interrupt_entry_set(vector, x86_exception_entries[vector],
                            X86_ACCESS_INTERRUPT_GATE);
  }
  x86_interrupt_entry_set(0x20, asm_inthandler20,
                          X86_ACCESS_INTERRUPT_GATE);
  x86_interrupt_entry_set(0x21, asm_inthandler21,
                          X86_ACCESS_INTERRUPT_GATE);
  x86_interrupt_entry_set(0x2c, asm_inthandler2c,
                          X86_ACCESS_INTERRUPT_GATE);
  x86_interrupt_entry_set(0x2e, asm_ide_irq, X86_ACCESS_INTERRUPT_GATE);
  x86_interrupt_entry_set(0x2f, asm_ide_irq, X86_ACCESS_INTERRUPT_GATE);
  x86_interrupt_entry_set(0x36, x86_syscall_entry,
                          X86_USER_ACCESS(X86_ACCESS_INTERRUPT_GATE));
  x86_interrupt_entry_set(X86_VECTOR_RESCHEDULE, x86_reschedule_entry,
                          X86_ACCESS_INTERRUPT_GATE);
  x86_interrupt_entry_set(X86_VECTOR_SMP_WAKE, x86_smp_wake_entry,
                          X86_ACCESS_INTERRUPT_GATE);

  x86_descriptor_table_pointer_t idt_pointer = {
      .limit = X86_IDT_LIMIT,
      .base = X86_IDT_ADDRESS,
  };
  x86_idt_load(&idt_pointer);
  irq_restore(state);
}

void arch_interrupt_init_secondary(void) {
  irq_state_t state = irq_save();
  x86_descriptor_table_pointer_t gdt_pointer = {
      .limit = X86_GDT_LIMIT,
      .base = X86_GDT_ADDRESS,
  };
  x86_descriptor_table_pointer_t idt_pointer = {
      .limit = X86_IDT_LIMIT,
      .base = X86_IDT_ADDRESS,
  };
  x86_gdt_load(&gdt_pointer);
  x86_data_segments_load(X86_SELECTOR(X86_GDT_KERNEL_DATA_INDEX));
  x86_idt_load(&idt_pointer);
  irq_restore(state);
}

bool interrupt_register_entry(unsigned vector, interrupt_entry_t entry) {
  if (vector >= X86_IDT_ENTRY_COUNT || entry == NULL) {
    return false;
  }
  irq_state_t state = irq_save();
  x86_interrupt_entry_set(vector, entry, X86_ACCESS_INTERRUPT_GATE);
  irq_restore(state);
  return true;
}

void arch_task_state_init(void) {
  uint32_t cpu = smp_current_cpu();
  x86_tss32_t *task_state = &task_states[cpu];
  memset(task_state, 0, sizeof(*task_state));
  task_state->ss0 = X86_SELECTOR(X86_GDT_KERNEL_DATA_INDEX);
  x86_segment_descriptor_set(&x86_gdt()[X86_GDT_TSS_INDEX + cpu],
                             sizeof(*task_state) - 1,
                             (uintptr_t)task_state, X86_ACCESS_TSS32);
  x86_task_register_load(X86_SELECTOR(X86_GDT_TSS_INDEX + cpu));
}

void arch_task_set_kernel_stack(uintptr_t stack_top) {
  task_states[smp_current_cpu()].esp0 = stack_top;
}

void x86_user_frame_init(x86_interrupt_frame_t *frame, uint32_t eip,
                         uint32_t esp) {
  frame->edi = 1;
  frame->esi = 2;
  frame->ebp = 3;
  frame->esp_dummy = 4;
  frame->ebx = 5;
  frame->edx = 6;
  frame->ecx = 7;
  frame->eax = 8;
  frame->gs = X86_USER_SELECTOR(X86_GDT_USER_BASE_INDEX);
  frame->fs = X86_USER_SELECTOR(X86_GDT_USER_DATA_INDEX);
  frame->es = X86_USER_SELECTOR(X86_GDT_USER_DATA_INDEX);
  frame->ds = X86_USER_SELECTOR(X86_GDT_USER_DATA_INDEX);
  frame->eip = eip;
  frame->cs = X86_USER_SELECTOR(X86_GDT_USER_CODE_INDEX);
  frame->eflags = 0x202u;
  frame->esp = esp;
  frame->ss = X86_USER_SELECTOR(X86_GDT_USER_DATA_INDEX);
}

void x86_bios_interrupt(uint8_t interrupt_number, regs16_t *registers) {
  irq_state_t state = irq_save();
  x86_segment_descriptor_t *gdt = x86_gdt();
  x86_segment_descriptor_set(&gdt[X86_GDT_BIOS_CODE32_INDEX], 0xffffffffu, 0,
                             X86_ACCESS_CODE32_ER);
  x86_segment_descriptor_set(&gdt[X86_GDT_BIOS_CODE16_INDEX], 0xfffffu, 0,
                             X86_ACCESS_CODE16_ER);
  x86_segment_descriptor_set(&gdt[X86_GDT_BIOS_DATA16_INDEX], 0xfffffu, 0,
                             X86_ACCESS_DATA16_RW);
  memcpy(0, IVT, 0x400);
  x86_bios_interrupt_raw(interrupt_number, registers);
  x86_segment_descriptor_set(&gdt[X86_GDT_BIOS_CODE32_INDEX], 0, 0, 0);
  x86_segment_descriptor_set(&gdt[X86_GDT_BIOS_CODE16_INDEX], 0, 0, 0);
  x86_segment_descriptor_set(&gdt[X86_GDT_BIOS_DATA16_INDEX], 0, 0, 0);
  x86_cr3_write(current_task()->pde);
  irq_restore(state);
}
