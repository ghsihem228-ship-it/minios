BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    mov eax, SYS_SOCKET
    mov ebx, SOCK_UDP
    int 0x80
    test eax, eax
    js fail
    mov edi, eax
    mov eax, SYS_CONNECT
    mov ebx, edi
    mov ecx, 0x0a000202
    mov edx, 9000
    int 0x80
    test eax, eax
    jnz fail
    mov eax, SYS_SEND
    mov ebx, edi
    mov ecx, message
    mov edx, message_len
    int 0x80
    cmp eax, message_len
    jne fail
    mov esi, 500
receive_again:
    mov eax, SYS_RECV
    mov ebx, edi
    mov ecx, buffer
    mov edx, 256
    int 0x80
    cmp eax, -11
    je retry
    cmp eax, message_len
    jne fail
    push edi
    mov esi, message
    mov edi, buffer
    mov ecx, message_len
    repe cmpsb
    pop edi
    jne fail
    mov eax, SYS_SOCK_CLOSE
    mov ebx, edi
    int 0x80
    print success, success_len
    exit 0
retry:
    dec esi
    jz fail
    mov eax, SYS_SLEEP
    mov ebx, 1
    int 0x80
    jmp receive_again
fail:
    print failure, failure_len
    exit 1
section .rodata
message: db "Hello from Ring 3 UDP!",10
message_len equ $-message
success: db "[user] UDP echo passed",10
success_len equ $-success
failure: db "[user] UDP echo failed",10
failure_len equ $-failure
section .bss
buffer: resb 256
