bits 64
default rel
; Offsets are checked against x64_interrupt_frame_t in task.h.
%define SIMD_SIZE 768
%define YMM_INUSE 464
%define FRAME_VECTOR (SIMD_SIZE + 15 * 8)
%define FRAME_CS (FRAME_VECTOR + 3 * 8)
section .text
extern x64_interrupt_dispatch
extern x64_syscall_dispatch
extern kernel_lock_leave
extern x64_xstate_mask
extern x64_xgetbv1
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
  sub rsp, SIMD_SIZE
  ; FXSAVE clears the AVX XINUSE bit. Read it first when the CPU exposes
  ; XGETBV(1), otherwise an interrupted YMM value would be silently lost.
  xor r11d, r11d
  test dword [rel x64_xstate_mask], 4
  jz %%save_legacy
  cmp dword [rel x64_xgetbv1], 0
  je %%mark_ymm
  mov ecx, 1
  xgetbv
  test eax, 4
  jz %%save_legacy
%%mark_ymm:
  mov r11d, 1
%%save_legacy:
  ; Reserved FXSAVE bytes are sanitized only when exporting a signal frame.
  fxsave64 [rsp]
  mov qword [rsp + YMM_INUSE], r11
  cmp qword [rsp + YMM_INUSE], 0
  je %%saved
%assign reg 0
%rep 16
  vextractf128 [rsp + 512 + reg * 16], ymm%+reg, 1
%assign reg reg + 1
%endrep
  ; C code uses SSE; clear upper lanes after preserving the interrupted state.
  vzeroupper
%%saved:
  ldmxcsr [kernel_mxcsr]
%endmacro
%macro RESTORE_REGISTERS 0
  fxrstor64 [rsp]
  test dword [rel x64_xstate_mask], 4
  jz %%restored
  cmp qword [rsp + YMM_INUSE], 0
  jne %%restore_ymm
  vzeroupper
  jmp %%restored
%%restore_ymm:
%assign reg 0
%rep 16
  vinsertf128 ymm%+reg, ymm%+reg, [rsp + 512 + reg * 16], 1
%assign reg reg + 1
%endrep
%%restored:
  add rsp, SIMD_SIZE
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
  mov rax, [rsp + FRAME_VECTOR]
  cmp eax, 2
  je .paranoid
  cmp eax, 8
  je .paranoid
  cmp eax, 18
  je .paranoid
  test byte [rsp + FRAME_CS], 3
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
  bts qword [rsp + FRAME_VECTOR], 63
.dispatch:
  mov rdi, rsp
  call x64_interrupt_dispatch

x64_restore:
  cmp qword [rsp + FRAME_VECTOR], 256
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
