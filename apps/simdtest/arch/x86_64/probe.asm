bits 64
default rel
section .text
global simd_yield_probe, simd_fork_probe, simd_reset_probe

simd_reset_probe:
  mov r9d, 1
  mov eax, 0x50
  jmp simd_probe
simd_yield_probe:
  xor r9d, r9d
  mov eax, 0x50
  jmp simd_probe
simd_fork_probe:
  xor r9d, r9d
  mov eax, 0x4a
simd_probe:
  push rbx
  push r12
  push r13
  push r14
  push r15
  sub rsp, 16
  mov r12, rdi
  mov r13, rsi
  mov r14d, edx
  mov r15, r8
  mov ebx, eax
  mov [rsp], ecx
  ldmxcsr [rsp]
%assign reg 0
%rep 16
  movdqu xmm%+reg, [r12 + reg * 16]
%assign reg reg + 1
%endrep
  fninit
  fld qword [r12]
  test r9d, r9d
  jz .loop
  mov eax, 0x2f
  syscall
.loop:
  mov eax, ebx
  syscall
  dec r14d
  jnz .loop
%assign reg 0
%rep 16
  movdqu [r13 + reg * 16], xmm%+reg
%assign reg reg + 1
%endrep
  stmxcsr [r15]
  ; Capture the x87 control/status/tag word as well as ST(0) when nonempty.
  ; FXSAVE needs only 16-byte alignment and is available on every x86_64 CPU.
  sub rsp, 512
  fxsave64 [rsp]
  mov rcx, [rsp]
  mov [r13 + 256], rcx
  test byte [rsp + 4], 0xff
  jz .empty
  fstp qword [r13 + 264]
.empty:
  add rsp, 512
  add rsp, 16
  pop r15
  pop r14
  pop r13
  pop r12
  pop rbx
  ret
section .note.GNU-stack noalloc noexec nowrite progbits
