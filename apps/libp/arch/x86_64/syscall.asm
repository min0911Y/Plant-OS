bits 64
default rel
section .text
%define SYSCALL_SIGNAL_RETURN 0x65

; C uses SysV AMD64. Plant syscalls use RAX, RDI, RSI, RDX, R10, R8, R9.
; The saved argument slots make each wrapper's API-specific ordering explicit.
%define a0 qword [rsp]
%define a1 qword [rsp+8]
%define a2 qword [rsp+16]
%define a3 qword [rsp+24]
%define a4 qword [rsp+32]
%define a5 qword [rsp+40]
%macro CALL 2-9 0,0,0,0,0,0,rax
global %1:function
%1:
  push r9
  push r8
  push rcx
  push rdx
  push rsi
  push rdi
  mov rax, %2
  mov rdi, %3
  mov rsi, %4
  mov rdx, %5
  mov r10, %6
  mov r8, %7
  mov r9, %8
  syscall
  mov rax, %9
  add rsp, 48
  ret
%endmacro

%define DATA_RESULT rdx
%include "../syscalls.inc"
CALL get_mouse, 0x0f

global get_xy
get_xy:
  mov eax, 0x0e
  syscall
  mov eax, edx
  shl eax, 16
  mov ax, si
  ret

global return_to_app:function (return_to_app.end - return_to_app)
return_to_app:
  mov rdi, rsp
  mov eax, SYSCALL_SIGNAL_RETURN
  syscall
  ud2
return_to_app.end:

global setjmp, longjmp
setjmp:
  mov [rdi], rbx
  mov [rdi+8], rbp
  mov [rdi+16], r12
  mov [rdi+24], r13
  mov [rdi+32], r14
  mov [rdi+40], r15
  lea rax, [rsp+8]
  mov [rdi+48], rax
  mov rax, [rsp]
  mov [rdi+56], rax
  stmxcsr [rdi+64]
  xor eax, eax
  ret
longjmp:
  mov eax, esi
  test eax, eax
  jnz .value
  inc eax
.value:
  mov rbx, [rdi]
  mov rbp, [rdi+8]
  mov r12, [rdi+16]
  mov r13, [rdi+24]
  mov r14, [rdi+32]
  mov r15, [rdi+40]
  mov rsp, [rdi+48]
  ldmxcsr [rdi+64]
  jmp [rdi+56]

section .note.GNU-stack noalloc noexec nowrite progbits
