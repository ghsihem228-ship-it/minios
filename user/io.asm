BITS 32
global _start
section .text
_start:
    mov dx, 0x80
    xor eax, eax
    out dx, al          ; must raise #GP: Ring 3 has no I/O permission
    ud2
section .bss
unused: resd 1
