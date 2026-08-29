[BITS 32]

section .text

extern api_yield
extern fork

global fputest_fork_preserving_fpu
global fputest_yield_preserving_fpu

fputest_fork_preserving_fpu:
  mov eax, [esp + 4]
  fld qword [eax]
  call fork
  mov edx, [esp + 8]
  fstp qword [edx]
  ret

fputest_yield_preserving_fpu:
  mov eax, [esp + 4]
  fld qword [eax]
  call api_yield
  call api_yield
  call api_yield
  call api_yield
  mov eax, [esp + 8]
  fstp qword [eax]
  ret
