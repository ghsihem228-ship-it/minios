BITS 64
section .rodata
global user_elf_0, user_elf_0_end
user_elf_0:
incbin "build/user-0.elf"
user_elf_0_end:
global user_elf_1, user_elf_1_end
user_elf_1:
incbin "build/user-1.elf"
user_elf_1_end:
global user_elf_2, user_elf_2_end
user_elf_2:
incbin "build/user-2.elf"
user_elf_2_end:
global user_elf_3, user_elf_3_end
user_elf_3:
incbin "build/user-3.elf"
user_elf_3_end:
global user_elf_4, user_elf_4_end
user_elf_4:
incbin "build/user-4.elf"
user_elf_4_end:
global user_elf_5, user_elf_5_end
user_elf_5:
incbin "build/user-5.elf"
user_elf_5_end:
global user_elf_6, user_elf_6_end
user_elf_6:
incbin "build/user-6.elf"
user_elf_6_end:
global user_elf_7, user_elf_7_end
user_elf_7:
incbin "build/user-7.elf"
user_elf_7_end:
global user_elf_8, user_elf_8_end
user_elf_8:
incbin "build/user-8.elf"
user_elf_8_end:
section .note.GNU-stack noalloc noexec nowrite progbits
