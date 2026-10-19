BITS 32
global _start
section .text
_start:
    mov byte [_start], 0 ; code is present and user-readable, but not writable
    ud2
section .bss
unused: resd 1
