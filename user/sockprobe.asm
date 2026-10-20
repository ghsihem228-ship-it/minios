; Validate ownership, pointer checks, size limits and per-process quotas.
BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    mov eax, SYS_SOCK_STATUS
    xor ebx, ebx
    int 0x80
    cmp eax, -9
    jne fail
    mov eax, SYS_SOCKET
    mov ebx, 99
    int 0x80
    cmp eax, -22
    jne fail
    mov eax, SYS_SOCKET
    mov ebx, SOCK_UDP
    int 0x80
    test eax, eax
    js fail
    mov edi, eax
    mov eax, SYS_BIND
    mov ebx, edi
    mov ecx, 20000
    int 0x80
    test eax, eax
    jnz fail
    mov eax, SYS_SEND
    mov ebx, edi
    mov ecx, 0x100000
    mov edx, 1
    int 0x80
    cmp eax, -14
    jne fail
    mov eax, SYS_RECV
    mov ebx, edi
    mov ecx, _start
    mov edx, 1
    int 0x80
    cmp eax, -14
    jne fail
    mov eax, SYS_SEND
    mov ebx, edi
    mov ecx, message
    mov edx, 1025
    int 0x80
    cmp eax, -7
    jne fail
    mov eax, SYS_CONNECT
    mov ebx, edi
    xor ecx, ecx
    mov edx, 9000
    int 0x80
    cmp eax, -22
    jne fail
    mov eax, SYS_BIND
    mov ebx, edi
    mov ecx, 67
    int 0x80
    cmp eax, -22
    jne fail
    mov eax, SYS_SOCK_CLOSE
    mov ebx, edi
    int 0x80
    test eax, eax
    jnz fail
    mov eax, SYS_SOCK_CLOSE
    mov ebx, edi
    int 0x80
    cmp eax, -9
    jne fail
    xor esi, esi
allocate:
    mov eax, SYS_SOCKET
    mov ebx, SOCK_UDP
    int 0x80
    cmp eax, esi
    jne fail
    inc esi
    cmp esi, 4
    jb allocate
    mov eax, SYS_SOCKET
    mov ebx, SOCK_UDP
    int 0x80
    cmp eax, -24
    jne fail
    ; Exit intentionally without closing: kernel reaping must reclaim all four.
    print message, message_len
    exit 0
fail:
    exit 1
section .rodata
message: db "[user] socket validation passed",10
message_len equ $-message
