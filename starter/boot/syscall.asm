BITS 64
section .text
global syscall_invoke
syscall_invoke:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9
    mov r9, [rsp + 8]
    int 0x80
    ret

; Unknown syscall exercises every input register without requiring valid args.
; RAX and RDX are outputs; every other GPR and saved DF/IF must survive IRETQ.
global syscall_register_probe
syscall_register_probe:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rax, -1
%macro LOAD 2
    mov %1, 0x1234567800000000 + %2
%endmacro
    LOAD rbx, 1
    LOAD rcx, 2
    LOAD rdx, 3
    LOAD rbp, 4
    LOAD rdi, 5
    LOAD rsi, 6
    LOAD r8, 7
    LOAD r9, 8
    LOAD r10, 9
    LOAD r11, 10
    LOAD r12, 11
    LOAD r13, 12
    LOAD r14, 13
    LOAD r15, 14
    std
    int 0x80
    test rdx, rdx
    jnz .bad_df
    pushfq
    pop rdx
    cld
    and edx, (1 << 10) | (1 << 9)
    cmp edx, (1 << 10) | (1 << 9)
    jne .bad
    cmp rax, -1
    jne .bad
%macro CHECK 2
    mov rax, 0x1234567800000000 + %2
    cmp %1, rax
    jne .bad
%endmacro
    CHECK rbx, 1
    CHECK rcx, 2
    CHECK rbp, 4
    CHECK rdi, 5
    CHECK rsi, 6
    CHECK r8, 7
    CHECK r9, 8
    CHECK r10, 9
    CHECK r11, 10
    CHECK r12, 11
    CHECK r13, 12
    CHECK r14, 13
    CHECK r15, 14
    mov eax, 1
    jmp .done
.bad_df:
    cld
.bad:
    xor eax, eax
.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
