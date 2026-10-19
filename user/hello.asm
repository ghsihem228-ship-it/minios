BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    mov eax, SYS_GETPID
    int 0x80
    test eax, eax
    jz fail
    print greeting, greeting_len
    mov eax, SYS_OPEN
    mov ebx, path
    xor ecx, ecx
    int 0x80
    test eax, eax
    js fail
    mov edi, eax
    mov ebx, edi
    mov eax, SYS_READ
    mov ecx, buffer
    mov edx, 256
    int 0x80
    test eax, eax
    jle fail
    mov ecx, eax
    mov eax, SYS_PRINT
    mov ebx, buffer
    int 0x80
    mov eax, SYS_CLOSE
    mov ebx, edi
    int 0x80
    exit 0
fail:
    exit 1
section .rodata
greeting: db "Hello from Ring 3!",10
greeting_len equ $-greeting
path: db "/etc/motd",0
section .bss
buffer: resb 256
