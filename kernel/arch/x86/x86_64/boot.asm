bits 64
default rel
section .text
global x64_boot_entry
extern KernelMain
x64_boot_entry:
  cli
  cld
  lea rsp, [boot_stack_top]
  xor ebp, ebp
  mov rax, cr0
  and rax, ~0xc
  or rax, 0x22
  mov cr0, rax
  mov rax, cr4
  or rax, 0x600
  mov cr4, rax
  fninit
  ldmxcsr [mxcsr_default]
  call KernelMain
.halt:
  cli
  hlt
  jmp .halt

section .rodata
mxcsr_default: dd 0x1f80
section .bss
align 16
boot_stack: resb 65536
boot_stack_top:
