#ifndef STARTER_ELF_H
#define STARTER_ELF_H
#include <stdint.h>
#include <stddef.h>
#define ELF_SEGMENTS 8u
struct elf_segment { uint64_t offset, address, filesz, memsz; uint32_t flags; };
struct elf_plan { uint64_t entry; unsigned count; struct elf_segment segments[ELF_SEGMENTS]; };
int elf_validate(const void *, size_t, struct elf_plan *);
#endif
