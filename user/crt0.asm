BITS 32
global _start
extern main
section .text
_start:
    cld
    xor ebp, ebp
    mov eax, [esp]       ; argc
    lea edx, [esp+4]     ; argv, followed by a NULL and empty envp
    and esp, -16
    sub esp, 8
    push edx
    push eax
    call main           ; i386 cdecl: ESP is 16-byte aligned before CALL
    mov ebx, eax
    mov eax, 2
    int 0x80
    ud2
section .note.GNU-stack noalloc noexec nowrite progbits
