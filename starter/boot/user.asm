BITS 64
section .text
global user_enter, user_return
extern user_host_sp, user_host_flags
user_enter:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq
    pop qword [rel user_host_flags]
    mov [rel user_host_sp], rsp
    ; Entry only uses explicit capabilities. Erase other caller register contents.
    push qword 0x33
    push rsi
    push qword 0x202
    push qword 0x2b
    push rdi
    mov rdi, rdx
    mov ax, 0x33
    mov ds, ax
    mov es, ax
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor ebp, ebp
    xor esi, esi
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq
user_return:
    mov dx, 0x10
    mov ds, dx
    mov es, dx
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
