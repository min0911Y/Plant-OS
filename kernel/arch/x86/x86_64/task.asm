bits 64
default rel
section .text
global arch_task_switch
global arch_task_start
extern x64_simd_switch

arch_task_switch:
  push rbp
  push rbx
  push r12
  push r13
  push r14
  push r15
  mov [rdi], rsp
  mov [rcx], r8
  mov rsp, rsi
  mov cr3, rdx
  mov rdi, r8
  jmp restore_context
arch_task_start:
  mov [rdx], rcx
  mov rsp, rdi
  mov cr3, rsi
  mov rdi, rcx
restore_context:
  mov rbx, rsp
  and rsp, -16
  call x64_simd_switch
  mov rsp, rbx
  pop r15
  pop r14
  pop r13
  pop r12
  pop rbx
  pop rbp
  ret
