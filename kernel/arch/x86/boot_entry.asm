[BITS 32]

section .multiboot2 align=8
header_start:
  dd 0xe85250d6
  dd 0
  dd header_end - header_start
  dd -(0xe85250d6 + (header_end - header_start))
  dw 0
  dw 0
  dd 8
header_end:

section .text.boot
global x86_boot_entry
extern KernelMain
extern x86_boot_prepare

x86_boot_entry:
  cli
  cld
  mov esi, eax
  mov edi, ebx
  lgdt [cs:x86_boot_gdtr]
  jmp 0x10:.segments_ready

.segments_ready:
  mov ax, 0x08
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax
  mov esp, x86_boot_stack_top
  xor ebp, ebp

  push edi
  push esi
  call x86_boot_prepare
  add esp, 8
  test eax, eax
  jnz .failed

  call KernelMain

.halt:
  cli
  hlt
  jmp .halt

.failed:
  mov edi, 0xb8000
  mov esi, x86_boot_error
  mov ah, 0x4f
.write_error:
  lodsb
  test al, al
  jz .halt
  stosw
  jmp .write_error

section .rodata.boot
align 8
x86_boot_gdt:
  dq 0
  dq 0x00cf92000000ffff
  dq 0x00cf9a000000ffff
x86_boot_gdt_end:
x86_boot_gdtr:
  dw x86_boot_gdt_end - x86_boot_gdt - 1
  dd x86_boot_gdt
x86_boot_error:
  db "plant os: invalid multiboot2 initramfs", 0

section .bss.boot_stack nobits align=16
  resb 64 * 1024
x86_boot_stack_top:
