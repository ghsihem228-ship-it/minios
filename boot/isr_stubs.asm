; isr_stubs.asm - low-level entry points for CPU exceptions 0-31
;
; Some exceptions push an error code, others do not. To give the C handler a
; uniform stack layout, stubs for exceptions WITHOUT an error code push a
; dummy 0 first. Every stub then pushes its interrupt number and jumps to
; isr_common, which saves the CPU state and calls isr_handler(struct regs *).

section .text

extern isr_handler
global idt_load

; void idt_load(uint32_t idt_ptr_address)
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0            ; dummy error code
    push dword %1           ; interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1           ; the CPU already pushed the error code
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; Hardware interrupts: IRQ n arrives on vector 32 + n (after the PIC remap).
; They share isr_common with the CPU exceptions.
%macro IRQ 2
global irq%1
irq%1:
    push dword 0            ; dummy error code
    push dword %2           ; interrupt number (vector)
    jmp isr_common
%endmacro

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

global syscall_entry
syscall_entry:
    push dword 0
    push dword 128
    jmp isr_common

isr_common:
    pusha                   ; push eax, ecx, edx, ebx, esp, ebp, esi, edi
    mov ax, ds
    push eax                ; save the data segment

    mov ax, 0x10            ; load the kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld                     ; C code expects the direction flag cleared
    push esp                ; argument: pointer to struct regs
    call isr_handler
    mov esp, eax            ; handler may select another task's saved frame

    pop eax                 ; restore the data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8              ; drop interrupt number and error code
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
