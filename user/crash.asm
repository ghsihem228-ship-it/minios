BITS 32
global _start
section .text
_start:
    mov eax, [0x100000] ; kernel page: present, but supervisor-only
    ud2
section .bss
unused: resd 1
