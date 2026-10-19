BITS 32
global _start
section .text
_start:
    mov eax, [0x403fbffc] ; immediately below the mapped 16 KiB stack
    ud2
section .bss
unused: resd 1
