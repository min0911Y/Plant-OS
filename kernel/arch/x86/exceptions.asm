[BITS 32]

section .text

extern x86_exception_dispatch

%macro X86_EXCEPTION_ENTRY 2
x86_exception_entry_%1:
%if %2
  push dword %1
%else
  push dword 0
  push dword %1
%endif
  jmp x86_exception_common
%endmacro

X86_EXCEPTION_ENTRY 0, 0
X86_EXCEPTION_ENTRY 1, 0
X86_EXCEPTION_ENTRY 2, 0
X86_EXCEPTION_ENTRY 3, 0
X86_EXCEPTION_ENTRY 4, 0
X86_EXCEPTION_ENTRY 5, 0
X86_EXCEPTION_ENTRY 6, 0
X86_EXCEPTION_ENTRY 7, 0
X86_EXCEPTION_ENTRY 8, 1
X86_EXCEPTION_ENTRY 9, 0
X86_EXCEPTION_ENTRY 10, 1
X86_EXCEPTION_ENTRY 11, 1
X86_EXCEPTION_ENTRY 12, 1
X86_EXCEPTION_ENTRY 13, 1
X86_EXCEPTION_ENTRY 14, 1
X86_EXCEPTION_ENTRY 15, 0
X86_EXCEPTION_ENTRY 16, 0
X86_EXCEPTION_ENTRY 17, 1
X86_EXCEPTION_ENTRY 18, 0
X86_EXCEPTION_ENTRY 19, 0
X86_EXCEPTION_ENTRY 20, 0
X86_EXCEPTION_ENTRY 21, 1
X86_EXCEPTION_ENTRY 22, 0
X86_EXCEPTION_ENTRY 23, 0
X86_EXCEPTION_ENTRY 24, 0
X86_EXCEPTION_ENTRY 25, 0
X86_EXCEPTION_ENTRY 26, 0
X86_EXCEPTION_ENTRY 27, 0
X86_EXCEPTION_ENTRY 28, 0
X86_EXCEPTION_ENTRY 29, 1
X86_EXCEPTION_ENTRY 30, 1
X86_EXCEPTION_ENTRY 31, 0

x86_exception_common:
  push ds
  push es
  push fs
  push gs
  pushad
  cld
  mov ax, 0x08
  mov ds, ax
  mov es, ax

  mov ebx, esp
  and esp, -16
  sub esp, 12
  push ebx
  call x86_exception_dispatch
  mov esp, ebx

  popad
  pop gs
  pop fs
  pop es
  pop ds
  add esp, 8
  iretd

section .rodata align=4

global x86_exception_entries
x86_exception_entries:
%assign vector 0
%rep 32
  dd x86_exception_entry_%+vector
%assign vector vector + 1
%endrep
