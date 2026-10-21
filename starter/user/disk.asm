; A small separately built executable loaded from a saved file, not the kernel.
BITS 64
section .text
global _start
_start:
    ; RDI is the filesystem root explicitly supplied by the trusted debug shell.
    mov eax, 7
    lea rsi, [rel name]
    mov edx, 9
    mov r10d, 2                 ; CAP_WRITE
    mov r8d, 1                  ; create
    xor r9d, r9d
    int 0x80
    test rax, rax
    jnz .bad
    mov rbx, rdx
    mov eax, 9
    mov rdi, rbx
    lea rsi, [rel message]
    mov edx, 9
    xor r10d, r10d
    xor r8d, r8d
    int 0x80
    test rax, rax
    jnz .bad
    mov eax, 1
    mov rdi, rbx
    xor esi, esi
    xor edx, edx
    int 0x80
    mov edi, 42
    jmp .exit
.bad:
    mov edi, 77
.exit:
    mov eax, 6
    xor esi, esi
    xor edx, edx
    xor r10d, r10d
    xor r8d, r8d
    xor r9d, r9d
    int 0x80
    ud2
name: db 'disk-note'
message: db 'from-disk'
section .data
    db 0
section .note.GNU-stack noalloc noexec nowrite progbits
