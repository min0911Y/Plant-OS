bits 64
default rel
section .text
extern x64_interrupt_dispatch
extern x64_syscall_dispatch
extern kernel_lock_leave
global x64_syscall_entry
global x64_return_to_user
global arch_task_interrupt_return

%macro SAVE_REGISTERS 0
  push rax
  push rbx
  push rcx
  push rdx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 512
  mov rdi, rsp
  xor eax, eax
  mov ecx, 64
  rep stosq
  ; A transient frame needs a complete image: XSAVEOPT's modified-state
  ; optimization cannot be used with a cleared or reused stack buffer.
  fxsave64 [rsp]
  ldmxcsr [kernel_mxcsr]
%endmacro
%macro RESTORE_REGISTERS 0
  fxrstor64 [rsp]
  add rsp, 512
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rdx
  pop rcx
  pop rbx
  pop rax
%endmacro

x64_syscall_entry:
  swapgs
  lfence
  mov [gs:8], rsp
  mov rsp, [gs:0]
  push qword 27
  push qword [gs:8]
  push r11
  push qword 35
  push rcx
  push qword 0
  push qword 256
  SAVE_REGISTERS
  mov rdi, rsp
  call x64_syscall_dispatch
  jmp x64_restore

%assign vector 0
%rep 256
x64_vector_%+vector:
%if vector != 8 && vector != 10 && vector != 11 && vector != 12 && vector != 13 && vector != 14 && vector != 17 && vector != 21 && vector != 29 && vector != 30
  push qword 0
%endif
  push qword vector
  jmp x64_interrupt_common
%assign vector vector + 1
%endrep

x64_interrupt_common:
  cld
  SAVE_REGISTERS
  mov rax, [rsp + 632]
  cmp eax, 2
  je .paranoid
  cmp eax, 8
  je .paranoid
  cmp eax, 18
  je .paranoid
  test byte [rsp + 656], 3
  jz .dispatch
  swapgs
  lfence
  jmp .dispatch
.paranoid:
  ; An NMI can interrupt either side of SWAPGS with a ring-0 CS.
  mov ecx, 0xc0000101
  rdmsr
  test edx, edx
  js .dispatch
  swapgs
  lfence
  bts qword [rsp + 632], 63
.dispatch:
  mov rdi, rsp
  call x64_interrupt_dispatch

x64_restore:
  cmp qword [rsp + 632], 256
  je .sysret
  RESTORE_REGISTERS
  bt qword [rsp], 63
  jc .swap
  test byte [rsp + 24], 3
  jz .iret
.swap:
  swapgs
.iret:
  add rsp, 16
  iretq
.sysret:
  RESTORE_REGISTERS
  mov rcx, [rsp + 16]
  mov r11, [rsp + 32]
  mov rsp, [rsp + 40]
  swapgs
  o64 sysret

arch_task_interrupt_return:
  ; The synthetic context leaves a SysV return slot before the entry frame.
  add rsp, 8
  call kernel_lock_leave
  jmp x64_restore

x64_return_to_user:
  cli
  mov rsp, rdi
  jmp x64_restore

section .rodata
align 4
kernel_mxcsr: dd 0x1f80
align 8
global x64_interrupt_entries
x64_interrupt_entries:
%assign vector 0
%rep 256
  dq x64_vector_%+vector
%assign vector vector + 1
%endrep
