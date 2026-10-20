BITS 64
section .text
extern interrupt_dispatch

; Uniform vector/error pair. Never invoke an error-code vector with INT n:
; software INT does not push the hardware exception error code.
%assign vec 0
%rep 256
global isr_%+vec
isr_%+vec:
%if vec != 8 && vec != 10 && vec != 11 && vec != 12 && vec != 13 && vec != 14 && vec != 17 && vec != 21 && vec != 29 && vec != 30
    push qword 0
%endif
    push qword vec
    jmp interrupt_common
%assign vec vec+1
%endrep

interrupt_common:
    ; Save before using any GPR. C may clobber every caller-saved register.
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld                         ; SysV requires DF=0; IRETQ restores saved DF.
    mov rdi, rsp
    mov rbx, rsp                 ; RBX is preserved by the C ABI.
    and rsp, -16                ; Interrupted code need not be call-aligned.
    call interrupt_dispatch
    mov rsp, rbx
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
    add rsp, 16                 ; Drop normalized vector/error.
    iretq

global interrupt_load_gdt
interrupt_load_gdt:
    lgdt [rdi]
    push qword 0x08
    lea rax, [rel .reload]
    push rax
    retfq
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov ax, 0x18
    ltr ax
    ret

global interrupt_register_probe, probe_breakpoint_resume
interrupt_register_probe:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov rax, 0x1122334400000000 + 1
    mov rbx, 0x1122334400000000 + 2
    mov rcx, 0x1122334400000000 + 3
    mov rdx, 0x1122334400000000 + 4
    mov rbp, 0x1122334400000000 + 5
    mov rsi, 0x1122334400000000 + 6
    mov rdi, 0x1122334400000000 + 7
    mov r8, 0x1122334400000000 + 8
    mov r9, 0x1122334400000000 + 9
    mov r10, 0x1122334400000000 + 10
    mov r11, 0x1122334400000000 + 11
    mov r12, 0x1122334400000000 + 12
    mov r13, 0x1122334400000000 + 13
    mov r14, 0x1122334400000000 + 14
    mov r15, 0x1122334400000000 + 15
    std
    int3
probe_breakpoint_resume:
    pushfq
    test qword [rsp], 1 << 10
    lea rsp, [rsp + 8]
    jz .bad
    cld
%macro CHECK 2
    cmp %1, [rel probe_expected + 8 * (%2 - 1)]
    jne .bad
%endmacro
%macro CHECK_ALL 0
    CHECK rax, 1
    CHECK rbx, 2
    CHECK rcx, 3
    CHECK rdx, 4
    CHECK rbp, 5
    CHECK rsi, 6
    CHECK rdi, 7
    CHECK r8, 8
    CHECK r9, 9
    CHECK r10, 10
    CHECK r11, 11
    CHECK r12, 12
    CHECK r13, 13
    CHECK r14, 14
    CHECK r15, 15
%endmacro
    CHECK_ALL
    ; A genuine IRQ arrives while all 15 registers hold full-width sentinels.
    sti
    hlt
    pushfq
    cli
    test qword [rsp], 1 << 9
    lea rsp, [rsp + 8]
    jz .bad
    CHECK_ALL
    mov eax, 1
    jmp .done
.bad:
    cli
    cld
    xor eax, eax
.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

global memory_stack_fault
memory_stack_fault:
    mov rsp, rdi                ; Deliberately put the next frame in a guard.
    ud2                         ; Fault delivery fails; #DF must use IST1.
.halt:
    cli
    hlt
    jmp .halt

section .rodata align=8
probe_expected:
%assign reg_value 1
%rep 15
    dq 0x1122334400000000 + reg_value
%assign reg_value reg_value+1
%endrep
global interrupt_stubs
interrupt_stubs:
%assign vec 0
%rep 256
    dq isr_%+vec
%assign vec vec+1
%endrep
section .note.GNU-stack noalloc noexec nowrite progbits
