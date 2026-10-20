BITS 64
section .text
global _start
extern user_main, syscall_invoke
_start:
    call user_main
    mov rdi, 6
    movsxd rsi, eax
    xor edx, edx
    xor ecx, ecx
    xor r8d, r8d
    xor r9d, r9d
    sub rsp, 16
    mov qword [rsp], 0
    call syscall_invoke
    ud2
section .note.GNU-stack noalloc noexec nowrite progbits
