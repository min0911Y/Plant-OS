bits 64
default rel
section .text
global simd_probe
global simd_signal_handler
extern simd_signal_avx, simd_signal_received

simd_signal_handler:
  ; No kernel stack padding or inactive YMM bytes may escape in a signal frame.
  ; ucontext.uc_mcontext starts at byte 48 (asserted by the C fixture).
  add rdx, 48
  movzx eax, byte [rdx + 5]
%assign offset 416
%rep 6
  or rax, [rdx + offset]
%assign offset offset + 8
%endrep
%assign offset 472
%rep 5
  or rax, [rdx + offset]
%assign offset offset + 8
%endrep
%assign reg 0
%rep 8
  mov ecx, [rdx + 32 + reg * 16 + 10]
  or rax, rcx
  movzx ecx, word [rdx + 32 + reg * 16 + 14]
  or rax, rcx
%assign reg reg + 1
%endrep
  cmp qword [rdx + 464], 0
  jne .checked
  mov ecx, 32
.check_ymm:
  or rax, [rdx + 512 + rcx * 8 - 8]
  loop .check_ymm
.checked:
  test rax, rax
  setnz al
  movzx eax, al
  inc eax
  mov [rel simd_signal_received], eax
  cmp dword [rel simd_signal_avx], 0
  je .sse
  vzeroall
.sse:
%assign reg 0
%rep 16
  pxor xmm%+reg, xmm%+reg
%assign reg reg + 1
%endrep
  fninit
  ldmxcsr [rel default_mxcsr]
  ret

simd_probe:
  push rbx
  push r12
  push r13
  push r14
  push r15
  sub rsp, 16
  mov r12, rdi
  mov r13, rsi
  mov r10d, r9d
  mov r9d, edx
  mov r14d, 400
  mov ebx, 0x50
  cmp r9d, 1                    ; PROBE_FORK
  jne .setup
  mov r14d, 1
  mov ebx, 0x4a
.setup:
  mov r15, r8
  mov [rsp], ecx
  ldmxcsr [rsp]
%assign reg 0
%rep 16
  movdqu xmm%+reg, [r12 + reg * 16]
%assign reg reg + 1
%endrep
  test r10d, r10d
  jz .loaded
%assign reg 0
%rep 16
  vinsertf128 ymm%+reg, ymm%+reg, [r12 + 256 + reg * 16], 1
%assign reg reg + 1
%endrep
  test r9d, 4                    ; PROBE_CLEAN
  jz .loaded
  vzeroupper
.loaded:
  and r9d, 3
  fninit
  fld qword [r12]
  cmp r9d, 3                    ; PROBE_SIGNAL
  jne .reset
  ; Publish readiness only after all tested registers are live.
  lea rdi, [rel signal_ready]
  mov eax, 0x48
  syscall
  jmp .loop
.reset:
  cmp r9d, 2                    ; PROBE_RESET
  jne .loop
  mov eax, 0x2f
  syscall
.loop:
  mov eax, ebx
  syscall
  ; Keep live registers across timer interrupts as well as explicit yields.
  mov ecx, 10000
.spin:
  dec ecx
  jnz .spin
  cmp r9d, 3
  jne .count
  cmp dword [rel simd_signal_received], 0
  je .loop
  jmp .capture
.count:
  dec r14d
  jnz .loop
.capture:
  test r10d, 2
  jz .capture_registers
  ; XGETBV must not replace fork's return value in RAX.
  push rax
  mov ecx, 1
  xgetbv
  mov [r13 + 528], rax
  pop rax
.capture_registers:
%assign reg 0
%rep 16
  movdqu [r13 + reg * 16], xmm%+reg
%assign reg reg + 1
%endrep
  test r10d, r10d
  jz .stored
%assign reg 0
%rep 16
  vextractf128 [r13 + 256 + reg * 16], ymm%+reg, 1
%assign reg reg + 1
%endrep
  vzeroupper
.stored:
  stmxcsr [r15]
  ; Capture the x87 control/status/tag word as well as ST(0) when nonempty.
  ; FXSAVE needs only 16-byte alignment and is available on every x86_64 CPU.
  sub rsp, 512
  fxsave64 [rsp]
  mov rcx, [rsp]
  mov [r13 + 512], rcx
  test byte [rsp + 4], 0xff
  jz .empty
  fstp qword [r13 + 520]
.empty:
  add rsp, 512
  add rsp, 16
  pop r15
  pop r14
  pop r13
  pop r12
  pop rbx
  ret
section .rodata
align 4
default_mxcsr: dd 0x1f80
signal_ready: db "SIMDTEST SIGNAL READY", 10, 0
section .note.GNU-stack noalloc noexec nowrite progbits
