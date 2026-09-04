[BITS 32]

%define SYSCALL_SOCKET 0x5e

GLOBAL socket_syscall

[SECTION .text]
socket_syscall:
    push ebx
    mov eax, SYSCALL_SOCKET
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    int 0x36
    pop ebx
    ret
