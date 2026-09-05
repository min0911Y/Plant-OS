[BITS 32]

section .text

extern apic_send_eoi
extern kernel_lock_enter
extern kernel_lock_leave
extern scheduler_reschedule_interrupt
extern signal_deal
extern x86_irq_dispatch
extern x86_syscall_dispatch

global null_inthandler
global x86_irq_entries
global x86_message_entries
global x86_reschedule_entry
global x86_smp_wake_entry
global x86_syscall_entry

%macro X86_FRAME_SAVE 0
  push ds
  push es
  push fs
  push gs
  pusha
  cld
  mov ax, ss
  mov ds, ax
  mov es, ax
%endmacro

%macro X86_FRAME_RESTORE 0
  popa
  pop gs
  pop fs
  pop es
  pop ds
%endmacro

null_inthandler:
  iretd

x86_syscall_entry:
  X86_FRAME_SAVE
  call kernel_lock_enter
  push esp
  call x86_syscall_dispatch
  add esp, 4
  call signal_deal
  call kernel_lock_leave
  X86_FRAME_RESTORE
  iretd

%macro X86_IRQ_ENTRY 1
x86_irq_entry_%1:
  X86_FRAME_SAVE
  call kernel_lock_enter
  push esp
  push dword %1
  call x86_irq_dispatch
  add esp, 8
  call kernel_lock_leave
  X86_FRAME_RESTORE
  iretd
%endmacro

%assign irq 0
%rep 24
  X86_IRQ_ENTRY irq
%assign irq irq + 1
%endrep

; Must match X86_MESSAGE_VECTOR_FIRST/END in interrupt_controller.h.
%assign irq 0x40
%rep 0xf0 - 0x40
  X86_IRQ_ENTRY irq
%assign irq irq + 1
%endrep

x86_reschedule_entry:
  X86_FRAME_SAVE
  call kernel_lock_enter
  call scheduler_reschedule_interrupt
  call kernel_lock_leave
  X86_FRAME_RESTORE
  iretd

x86_smp_wake_entry:
  X86_FRAME_SAVE
  call apic_send_eoi
  X86_FRAME_RESTORE
  iretd

section .rodata align=4
x86_irq_entries:
%assign irq 0
%rep 24
  dd x86_irq_entry_%+irq
%assign irq irq + 1
%endrep

x86_message_entries:
%assign irq 0x40
%rep 0xf0 - 0x40
  dd x86_irq_entry_%+irq
%assign irq irq + 1
%endrep
