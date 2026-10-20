; Keeps one live TCP connection for kill/fault/resource-cleanup tests.
BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    mov eax, SYS_SOCKET
    mov ebx, SOCK_TCP
    int 0x80
    test eax, eax
    js fail
    mov edi, eax
    mov eax, SYS_CONNECT
    mov ebx, edi
    mov ecx, 0x0a000202
    mov edx, 8080
    int 0x80
    cmp eax, -115
    jne fail
again:
    mov eax, SYS_SLEEP
    mov ebx, 10
    int 0x80
    jmp again
fail:
    exit 1
