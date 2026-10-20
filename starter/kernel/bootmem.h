#ifndef STARTER_BOOTMEM_H
#define STARTER_BOOTMEM_H
#include <stddef.h>
#include <stdint.h>
#define PAGE_SIZE UINT64_C(4096)
#define PHYS_LIMIT UINT64_C(0x40000000) /* Early manager supports the first 1 GiB. */
#define BOOT_MAX_RANGES 128
#define BOOT_MAX_MODULES 32
struct phys_range { uint64_t base, length; };
struct memory_range { uint64_t base, length; uint32_t type; };
struct boot_memory {
    struct memory_range ranges[BOOT_MAX_RANGES];
    unsigned count;
};
/* Pure bounded decoder: Multiboot v1 variable-size mmap records. */
int bootmem_parse(const void *bytes, size_t length, struct boot_memory *out);
/* Capture all boot data needed by the manager before allocating anything. */
int bootmem_capture(uint32_t info_address, struct boot_memory *out,
                    struct phys_range *reserved, unsigned *reserved_count);
#endif
