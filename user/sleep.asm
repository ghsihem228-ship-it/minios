BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    mov eax, SYS_TICKS
    int 0x80
    mov esi, eax
    mov eax, SYS_SLEEP
    mov ebx, 20
    int 0x80
    mov eax, SYS_TICKS
    int 0x80
    sub eax, esi
    cmp eax, 20
    jb fail
    print message, message_len
    exit 0
fail:
    exit 1
section .rodata
message: db "[user] sleep/wakeup passed",10
message_len equ $-message
section .bss
unused: resd 1
