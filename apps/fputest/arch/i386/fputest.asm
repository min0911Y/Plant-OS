[BITS 32]

section .text

extern _GLOBAL_OFFSET_TABLE_
extern api_yield
extern fork

global fputest_fork_preserving_fpu
global fputest_yield_preserving_fpu

fputest_fork_preserving_fpu:
  push ebx
  call .got
.got:
  pop ebx
  add ebx, _GLOBAL_OFFSET_TABLE_ + $$ - .got wrt ..gotpc
  sub esp, 8
  mov eax, [esp + 16]
  fld qword [eax]
  call fork wrt ..plt
  mov edx, [esp + 20]
  fstp qword [edx]
  add esp, 8
  pop ebx
  ret

fputest_yield_preserving_fpu:
  push ebx
  call .got
.got:
  pop ebx
  add ebx, _GLOBAL_OFFSET_TABLE_ + $$ - .got wrt ..gotpc
  sub esp, 8
  mov eax, [esp + 16]
  fld qword [eax]
  call api_yield wrt ..plt
  call api_yield wrt ..plt
  call api_yield wrt ..plt
  call api_yield wrt ..plt
  mov eax, [esp + 20]
  fstp qword [eax]
  add esp, 8
  pop ebx
  ret

section .note.GNU-stack noalloc noexec nowrite progbits
