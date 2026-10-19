BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    ; Check the privilege level actually used to execute this program.
    mov ax, cs
    and eax, 3
    cmp eax, 3
    jne fail
    print 0x100000, 16
    cmp eax, -14
    jne fail
    print 0, 1
    cmp eax, -14
    jne fail
    print 0x403fffff, 2
    cmp eax, -14
    jne fail
    print message, 1025
    cmp eax, -7
    jne fail
    mov eax, 999
    int 0x80
    cmp eax, -38
    jne fail
    mov eax, SYS_CLOSE
    mov ebx, 99
    int 0x80
    cmp eax, -9
    jne fail
    mov eax, SYS_OPEN
    mov ebx, path
    xor ecx, ecx
    int 0x80
    test eax, eax
    js fail
    mov edi, eax
    mov ebx, edi
    mov eax, SYS_READ
    mov ecx, _start       ; Read-only code must not be a write destination.
    mov edx, 1
    int 0x80
    cmp eax, -14
    jne fail
    mov eax, SYS_CLOSE
    mov ebx, edi
    int 0x80
    ; BSS is zeroed, and a writable user stack works.
    cmp dword [zeroed], 0
    jne fail
    push dword 0x12345678
    pop edx
    cmp edx, 0x12345678
    jne fail
    mov eax, SYS_YIELD
    int 0x80
    print message, message_len
    exit 0
fail:
    exit 1
section .rodata
message: db "[user] syscall validation passed",10
message_len equ $-message
path: db "/etc/motd",0
section .bss
zeroed: resd 1
