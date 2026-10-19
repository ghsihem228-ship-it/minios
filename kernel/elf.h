#ifndef ELF_H
#define ELF_H
#include <stdint.h>
/* Load a checked i386 ET_EXEC image into a fresh process address space. */
int elf_load(unsigned slot, const uint8_t *image, uint32_t len, uint32_t *entry);
int elf_self_test(void);
#endif
