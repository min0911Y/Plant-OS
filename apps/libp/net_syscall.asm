[BITS 32]

%define SYSCALL_NETWORK 0x5e
%define NET_OPEN 1
%define NET_CLOSE 2
%define NET_CONFIGURE 3
%define NET_SEND 4
%define NET_RECV 5
%define NET_CONNECT 6
%define NET_LISTEN 7
%define NET_GET_IP 8
%define NET_PING 9

GLOBAL Socket_Alloc, Socket_Init, Socket_Free, Socket_Send, Socket_Recv
GLOBAL GetIP, ping, listen, connect

[SECTION .text]
Socket_Alloc:
    push ebx
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_OPEN
    mov ecx, [esp + 8]
    int 0x36
    pop ebx
    ret

Socket_Init:
    push ebx
    push esi
    push edi
    push ebp
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_CONFIGURE
    mov ecx, [esp + 20]
    mov edx, [esp + 24]
    mov esi, [esp + 28]
    mov edi, [esp + 32]
    mov ebp, [esp + 36]
    int 0x36
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

Socket_Free:
    push ebx
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_CLOSE
    mov ecx, [esp + 8]
    int 0x36
    pop ebx
    ret

Socket_Send:
    push ebx
    push esi
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_SEND
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    mov esi, [esp + 20]
    int 0x36
    pop esi
    pop ebx
    ret

Socket_Recv:
    push ebx
    push esi
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_RECV
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    mov esi, [esp + 20]
    int 0x36
    pop esi
    pop ebx
    ret

GetIP:
    push ebx
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_GET_IP
    int 0x36
    pop ebx
    ret

ping:
    push ebx
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_PING
    mov ecx, [esp + 8]
    int 0x36
    pop ebx
    ret

listen:
    push ebx
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_LISTEN
    mov ecx, [esp + 8]
    int 0x36
    pop ebx
    ret

connect:
    push ebx
    mov eax, SYSCALL_NETWORK
    mov ebx, NET_CONNECT
    mov ecx, [esp + 8]
    int 0x36
    pop ebx
    ret
