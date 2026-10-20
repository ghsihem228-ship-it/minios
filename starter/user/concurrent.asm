; CPU-bound isolation fixture. It never yields or makes a syscall unless a
; register, private memory value, x87 state, or XMM state changes unexpectedly.
BITS 64
%ifndef PROBE
%define PROBE 9
%endif
section .text
global _start
_start:
    mov qword [rel marker], PROBE
    mov r12, 0x123456789abcdef0
    mov rbx, 0x2badc0de12345678
    movdqu xmm0, [rel signature]
    movdqu xmm8, [rel signature]
    fldcw [rel control]
    ldmxcsr [rel mxcsr]
%if PROBE = 9
    fld1
%else
    fldz
%endif
.loop:
    cmp qword [rel marker], PROBE
    jne .bad
    mov rax, 0x123456789abcdef0
    cmp r12, rax
    jne .bad
    mov rax, 0x2badc0de12345678
    cmp rbx, rax
    jne .bad
    movdqu [rel observed], xmm0
    mov rax, [rel signature]
    cmp [rel observed], rax
    jne .bad
    mov rax, [rel signature + 8]
    cmp [rel observed + 8], rax
    jne .bad
    movdqu [rel observed], xmm8
    mov rax, [rel signature]
    cmp [rel observed], rax
    jne .bad
    mov rax, [rel signature + 8]
    cmp [rel observed + 8], rax
    jne .bad
    fnstcw [rel observed]
    mov ax, [rel control]
    cmp [rel observed], ax
    jne .bad
    stmxcsr [rel observed]
    mov eax, [rel mxcsr]
    cmp [rel observed], eax
    jne .bad
    fst qword [rel observed]
%if PROBE = 9
    mov rax, 0x3ff0000000000000
%else
    xor eax, eax
%endif
    cmp [rel observed], rax
    jne .bad
    inc qword [rel counter]
    jmp .loop
.bad:
    mov eax, 6
    mov edi, 111
    xor esi, esi
    xor edx, edx
    xor r10d, r10d
    xor r8d, r8d
    xor r9d, r9d
    int 0x80
    ud2
section .data align=16
; First data page is 0x40001000; kernel self-test independently reads these.
counter: dq 0
marker: dq 0
signature: dq PROBE, 0xfedcba9876543210 + PROBE
observed: times 16 db 0
%if PROBE = 9
control: dw 0x37f
mxcsr: dd 0x1f80
%else
control: dw 0x77f
mxcsr: dd 0x3f80
%endif
section .note.GNU-stack noalloc noexec nowrite progbits
