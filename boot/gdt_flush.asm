; gdt_flush.asm - load the new GDT and reload all segment registers

section .text
global gdt_flush

; void gdt_flush(uint32_t gdt_ptr_address)
gdt_flush:
    mov eax, [esp + 4]      ; argument: address of the GDT pointer
    lgdt [eax]              ; tell the CPU where the GDT is

    mov ax, 0x10            ; 0x10 = data segment selector (entry 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.reload_cs     ; far jump reloads CS with the code selector (entry 1)
.reload_cs:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
