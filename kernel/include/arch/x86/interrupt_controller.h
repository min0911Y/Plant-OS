#ifndef KERNEL_X86_INTERRUPT_CONTROLLER_H
#define KERNEL_X86_INTERRUPT_CONTROLLER_H

/* Keep message vectors separate from wired IRQs, the i386 syscall gate,
 * the scheduler/wake IPIs and the APIC spurious vector. */
#define X86_MESSAGE_VECTOR_FIRST 0x40
#define X86_MESSAGE_VECTOR_END 0xf0
#define X86_MESSAGE_VECTOR_COUNT (X86_MESSAGE_VECTOR_END - X86_MESSAGE_VECTOR_FIRST)

/* Legacy 8259 ports shared by the x86 interrupt-controller implementation. */
#define X86_PIC0_COMMAND 0x20
#define X86_PIC0_DATA 0x21
#define X86_PIC1_COMMAND 0xa0
#define X86_PIC1_DATA 0xa1

#endif
