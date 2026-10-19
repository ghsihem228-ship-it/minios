BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    mov eax, SYS_OPEN
    mov ebx, path
    mov ecx, 7          ; write | create | truncate
    int 0x80
    test eax, eax
    js fail
    mov edi, eax
    mov eax, SYS_WRITE
    mov ebx, edi
    mov ecx, contents
    mov edx, contents_len
    int 0x80
    cmp eax, contents_len
    jne fail
    mov eax, SYS_SEEK
    mov ebx, edi
    xor ecx, ecx
    int 0x80
    test eax, eax
    jnz fail
    mov eax, SYS_READ
    mov ebx, edi
    mov ecx, buffer
    mov edx, 64
    int 0x80
    cmp eax, contents_len
    jne fail
    mov esi, contents
    mov edi, buffer
    mov ecx, contents_len
    repe cmpsb
    jne fail
    ; Deliberately leave the descriptor open: exit must close it.
    print message, message_len
    exit 0
fail:
    exit 1
section .rodata
path: db "/tmp/user.txt",0
contents: db "Written by a Ring 3 program.",10
contents_len equ $-contents
message: db "[user] filesystem roundtrip passed",10
message_len equ $-message
section .bss
buffer: resb 64
