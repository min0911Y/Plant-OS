[BITS 16]

section .text align=16

%define SMP_TRAMPOLINE_PHYS 0x6000
%define TRAMPOLINE_ADDRESS(label) \
  (SMP_TRAMPOLINE_PHYS + label - x86_smp_trampoline_start)

global x86_smp_trampoline_start
global x86_smp_trampoline_end
global x86_smp_trampoline_stack
global x86_smp_trampoline_cr3
global x86_smp_trampoline_cpu
global x86_smp_trampoline_entry

x86_smp_trampoline_start:
  cli
  cld
  xor ax, ax
  mov ds, ax
  mov es, ax
  mov ss, ax
  mov sp, SMP_TRAMPOLINE_PHYS
  lgdt [TRAMPOLINE_ADDRESS(x86_smp_trampoline_gdtr)]

  mov eax, cr0
  or eax, 1
  mov cr0, eax
  jmp 0x10:TRAMPOLINE_ADDRESS(x86_smp_trampoline_protected)

[BITS 32]
x86_smp_trampoline_protected:
  mov ax, 0x08
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax
  mov esp, [TRAMPOLINE_ADDRESS(x86_smp_trampoline_stack)]

  mov eax, [TRAMPOLINE_ADDRESS(x86_smp_trampoline_cr3)]
  mov cr3, eax
  mov eax, cr0
  or eax, 0x80010021
  mov cr0, eax
  jmp 0x10:TRAMPOLINE_ADDRESS(x86_smp_trampoline_paged)

x86_smp_trampoline_paged:
  push dword [TRAMPOLINE_ADDRESS(x86_smp_trampoline_cpu)]
  call dword [TRAMPOLINE_ADDRESS(x86_smp_trampoline_entry)]

x86_smp_trampoline_halt:
  cli
  hlt
  jmp x86_smp_trampoline_halt

align 8
x86_smp_trampoline_gdt:
  dq 0
  dq 0x00cf92000000ffff
  dq 0x00cf9a000000ffff
x86_smp_trampoline_gdt_end:

x86_smp_trampoline_gdtr:
  dw x86_smp_trampoline_gdt_end - x86_smp_trampoline_gdt - 1
  dd TRAMPOLINE_ADDRESS(x86_smp_trampoline_gdt)

align 4
x86_smp_trampoline_stack:
  dd 0
x86_smp_trampoline_cr3:
  dd 0
x86_smp_trampoline_cpu:
  dd 0
x86_smp_trampoline_entry:
  dd 0

x86_smp_trampoline_end:
