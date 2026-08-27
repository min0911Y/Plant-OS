[BITS 32]

GLOBAL x86_return_to_user

SECTION .text
x86_return_to_user:
  cli
  mov esi, [esp + 4]
  sub esp, 17 * 4
  mov edi, esp
  mov ecx, 17
  cld
  rep movsd
  popad
  pop gs
  pop fs
  pop es
  pop ds
  iretd
