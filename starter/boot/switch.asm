BITS 64
section .text
; SysV function boundary: caller-saved GPRs are already the caller's concern.
; No x87/SIMD/TLS state is supported. Both contexts have IF=0 during switch.
global context_switch
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
