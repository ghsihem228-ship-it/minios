BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    xor ecx, ecx
fill:
    mov [pattern+ecx], cl
    inc ecx
    cmp ecx, 1024
    jb fill
    mov eax, SYS_OPEN
    mov ebx, path
    mov ecx, 7
    int 0x80
    test eax, eax
    js fail
    mov ebp, eax
    mov dword [round], 12
write_loop:
    mov eax, SYS_WRITE
    mov ebx, ebp
    mov ecx, pattern
    mov edx, 1024
    int 0x80
    cmp eax, 1024
    jne fail
    dec dword [round]
    jnz write_loop
    mov eax, SYS_UNLINK
    mov ebx, path
    int 0x80
    cmp eax, -16
    jne fail
    mov eax, SYS_SEEK
    mov ebx, ebp
    xor ecx, ecx
    int 0x80
    test eax, eax
    jnz fail
    mov dword [round], 12
read_loop:
    mov eax, SYS_READ
    mov ebx, ebp
    mov ecx, buffer
    mov edx, 1024
    int 0x80
    cmp eax, 1024
    jne fail
    mov esi, pattern
    mov edi, buffer
    mov ecx, 1024
    repe cmpsb
    jne fail
    dec dword [round]
    jnz read_loop
    mov eax, SYS_SEEK
    mov ebx, ebp
    mov ecx, 510
    int 0x80
    cmp eax, 510
    jne fail
    mov eax, SYS_WRITE
    mov ebx, ebp
    mov ecx, boundary
    mov edx, 8
    int 0x80
    cmp eax, 8
    jne fail
    mov eax, SYS_SEEK
    mov ebx, ebp
    mov ecx, 20000
    int 0x80
    cmp eax, 20000
    jne fail
    mov eax, SYS_WRITE
    mov ebx, ebp
    mov ecx, ending
    mov edx, 3
    int 0x80
    cmp eax, 3
    jne fail
    mov eax, SYS_STAT
    mov ebx, path
    mov ecx, stat
    int 0x80
    test eax, eax
    jnz fail
    cmp dword [stat], 20003
    jne fail
    mov eax, SYS_SEEK
    mov ebx, ebp
    mov ecx, 12288
    int 0x80
    mov eax, SYS_READ
    mov ebx, ebp
    mov ecx, buffer
    mov edx, 1024
    int 0x80
    cmp eax, 1024
    jne fail
    xor ecx, ecx
check_zero:
    cmp byte [buffer+ecx], 0
    jne fail
    inc ecx
    cmp ecx, 1024
    jb check_zero
    mov eax, SYS_CLOSE
    mov ebx, ebp
    int 0x80
    test eax, eax
    jnz fail
    mov eax, SYS_SYNC
    int 0x80
    test eax, eax
    jnz fail
    print message, message_len
    exit 0
fail:
    exit 1
section .rodata
path: db "/disk/user.dat",0
boundary: db "BOUNDARY"
ending: db "END"
message: db "[user] persistent disk roundtrip passed",10
message_len equ $-message
section .bss
pattern: resb 1024
buffer: resb 1024
stat: resd 2
round: resd 1
