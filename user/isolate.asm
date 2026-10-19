BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    cmp dword [owner], 0
    jne fail
    mov eax, SYS_GETPID
    int 0x80
    mov ebp, eax
    mov [owner], eax
    mov eax, SYS_TICKS
    int 0x80
    mov esi, eax
again:
    mov ecx, 100000
busy:
    cmp [owner], ebp
    jne fail
    dec ecx
    jnz busy
    mov eax, SYS_TICKS
    int 0x80
    sub eax, esi
    cmp eax, 200
    jb again
    print message, message_len
    exit 0
fail:
    exit 1
section .rodata
message: db "[user] address-space isolation passed",10
message_len equ $-message
section .bss
owner: resd 1
