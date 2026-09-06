bits 32
section .text

; i386 SysV preserves EBX (the PIC GOT), ESI, EDI and EBP. All syscall
; wrappers save these before assigning the Plant register arguments.
%define a0 dword [esp+20]
%define a1 dword [esp+24]
%define a2 dword [esp+28]
%define a3 dword [esp+32]
%define a4 dword [esp+36]
%define a5 dword [esp+40]
%macro CALL 2-9 0,0,0,0,0,0,eax
global %1:function (%1.end - %1)
%1:
  push ebx
  push esi
  push edi
  push ebp
  mov eax, %2
  mov ebx, %3
  mov ecx, %4
  mov edx, %5
  mov esi, %6
  mov edi, %7
  mov ebp, %8
  int 0x36
  mov eax, %9
  pop ebp
  pop edi
  pop esi
  pop ebx
  ret
%1.end:
%endmacro

%define DATA_RESULT edx
%include "../syscalls.inc"

global get_mouse:function
get_mouse:
  push ebx
  push esi
  push edi
  push ebp
  mov eax, 0x0f
  int 0x36
  mov al, cl
  mov ah, dl
  shl eax, 16
  mov ax, si
  pop ebp
  pop edi
  pop esi
  pop ebx
  ret

global get_xy:function
get_xy:
  push ebx
  push esi
  push edi
  push ebp
  mov eax, 0x0e
  int 0x36
  mov eax, edx
  shl eax, 16
  mov ax, cx
  pop ebp
  pop edi
  pop esi
  pop ebx
  ret

global return_to_app:function (return_to_app.end - return_to_app)
return_to_app:
  popa
  pop gs
  pop fs
  pop es
  pop ds
  ret
return_to_app.end:

global setjmp
; int setjmp(jmp_buf env);
setjmp:
    mov ecx, [esp + 4]  ; ecx = env
    mov edx, [esp + 0]  ; edx = ret addr
    mov [ecx + 0], edx
    mov [ecx + 4], ebx
    mov [ecx + 8], esp
    mov [ecx + 12], ebp
    mov [ecx + 16], esi
    mov [ecx + 20], edi
    mov [ecx + 24], eax ; eax = trigblock()'s ret val

    xor eax, eax    ; setjmp ret val = 0
    ret

global longjmp
; void longjmp(jmp_buf env, int val)
longjmp:

    mov edx, [esp + 4]  ; edx = env
    mov eax, [esp + 8]  ; eax = val
    mov ecx, [edx + 0]  ; ecx = setjmp()'s ret val 
    mov ebx, [edx + 4]
    mov esp, [edx + 8]
    mov ebp, [edx + 12]
    mov esi, [edx + 16]
    mov edi, [edx + 20]
    
    ; make sure longjmp's ret val not 0
    test eax, eax   ; if eax == 0:
    jnz .1          ;   eax += 1
    inc eax         ; else: goto lable 1
.1: ; let longjmp's ret addr as setjmp's ret addr
    mov [esp + 0], ecx ; ret addr = ecx = setjmp's next code
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
