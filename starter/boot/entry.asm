; Multiboot v1 enters 32-bit protected mode; x86_64 C needs long mode.
; These static tables are only bootstrap mappings, not a memory manager.
BITS 32
MB_MAGIC equ 0x1BADB002
MB_FLAGS equ 0x00010003       ; page alignment, memory info, explicit load addresses
COM1 equ 0x3F8
CODE64 equ 0x08
DATA equ 0x10

section .multiboot align=4
header:
    dd MB_MAGIC, MB_FLAGS, -(MB_MAGIC + MB_FLAGS)
    dd header, __load_start, __load_end, __bss_end, _start

section .text
global _start
extern kernel_main
extern __load_start, __load_end, __bss_start, __bss_end
_start:
    cli
    cld
    cmp eax, 0x2BADB002
    jne bad_magic
    ; Zero our BSS explicitly, before using any stack memory.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd
    mov esp, stack_top

    ; Detect CPUID without executing it on an unsupported processor.
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    test eax, 1 << 21
    jz no_cpuid
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz no_long_mode

    ; PML4[0] -> PDPT[0] -> 512 supervisor 2-MiB identity mappings.
    mov eax, pdpt
    or eax, 3
    mov [pml4], eax
    mov eax, pd
    or eax, 3
    mov [pdpt], eax
    xor ecx, ecx
.map:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83           ; present, writable, large page; user bit is clear
    mov [pd + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map

    lgdt [gdt_pointer]
    mov eax, cr4
    or eax, 1 << 5         ; PAE
    mov cr4, eax
    mov eax, pml4
    mov cr3, eax
    mov ecx, 0xC0000080    ; IA32_EFER
    rdmsr
    or eax, 1 << 8         ; LME
    wrmsr
    mov eax, cr0
    or eax, 1 << 31        ; PG; protected mode was established by the loader
    mov cr0, eax
    jmp CODE64:long_mode

bad_magic:
    mov esi, magic_error
    jmp boot_error
no_cpuid:
    mov esi, cpuid_error
    jmp boot_error
no_long_mode:
    mov esi, mode_error
boot_error:
    ; No 64-bit C call is safe yet. Initialize COM1 and report a fixed cause.
    mov dx, COM1+1
    xor al, al
    out dx, al
    mov dx, COM1+3
    mov al, 0x80
    out dx, al
    mov dx, COM1
    mov al, 1
    out dx, al
    mov dx, COM1+1
    xor al, al
    out dx, al
    mov dx, COM1+3
    mov al, 3
    out dx, al
    mov dx, COM1+2
    mov al, 0xC7
    out dx, al
    mov dx, COM1+4
    mov al, 3
    out dx, al
.next:
    lodsb
    test al, al
    jz .halt
    mov bl, al
    mov ecx, 100000
.wait:
    mov dx, COM1+5
    in al, dx
    test al, 0x20
    jnz .send
    loop .wait
    jmp .halt
.send:
    mov dx, COM1
    mov al, bl
    out dx, al
    jmp .next
.halt:
    cli
    hlt
    jmp .halt

BITS 64
long_mode:
    mov ax, DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov rsp, stack_top
    and rsp, -16           ; SysV AMD64: RSP is aligned before CALL
    xor ebp, ebp
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

section .rodata
align 8
gdt:
    dq 0
    dq 0x00AF9A000000FFFF   ; 64-bit ring-0 code, L=1 and D=0
    dq 0x00CF92000000FFFF   ; ring-0 data
.end:
gdt_pointer:
    dw gdt.end-gdt-1
    dd gdt                 ; LGDT executes in 32-bit mode, below 4 GiB
magic_error: db "Boot error: invalid Multiboot entry",13,10,0
cpuid_error: db "Boot error: CPUID unsupported",13,10,0
mode_error: db "Boot error: long mode unsupported",13,10,0

section .bss align=4096
pml4: resb 4096
pdpt: resb 4096
pd: resb 4096
alignb 16
stack_bottom: resb 16384
stack_top:
section .note.GNU-stack noalloc noexec nowrite progbits
