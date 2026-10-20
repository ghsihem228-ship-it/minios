BITS 64
section .text
global user_return
extern user_host_sp, user_host_flags
global user_resume
user_resume:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq
    pop qword [rel user_host_flags]
    mov [rel user_host_sp], rsp
    mov ax, 0x33
    mov ds, ax
    mov es, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    ; Read only trusted saved frames; user code never supplies CS/RFLAGS here.
    mov rsp, rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
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
