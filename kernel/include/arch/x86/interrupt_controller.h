#ifndef KERNEL_X86_INTERRUPT_CONTROLLER_H
#define KERNEL_X86_INTERRUPT_CONTROLLER_H

/* Legacy 8259 ports shared by the x86 interrupt-controller implementation. */
#define X86_PIC0_COMMAND 0x20
#define X86_PIC0_DATA 0x21
#define X86_PIC1_COMMAND 0xa0
#define X86_PIC1_DATA 0xa1

#endif
