BITS 32
%include "user/abi.inc"
global _start
section .text
_start:
    ; No syscalls or yields: only timer preemption can let another task run.
    inc dword [counter]
    jmp _start
section .bss
counter: resd 1
