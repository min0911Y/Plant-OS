[BITS 32]

%define SYSCALL_VFS 0x1a

GLOBAL vfs_syscall

[SECTION .text]
vfs_syscall:
    push ebx
    mov eax, SYSCALL_VFS
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    int 0x36
    pop ebx
    ret
