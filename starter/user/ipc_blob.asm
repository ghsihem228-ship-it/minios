BITS 64
section .rodata
global ipc_elf, ipc_elf_end
ipc_elf:
incbin "build/user-13.embed.elf"
ipc_elf_end:
section .note.GNU-stack noalloc noexec nowrite progbits
