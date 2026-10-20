#include "bootmem.h"

/* The Multiboot loader is trusted firmware input. Bounds below ensure all
 * reads fit the temporary identity window; they do not authenticate firmware. */
static int readable(uint64_t address, uint64_t size) {
    return address >= PAGE_SIZE && size && address < PHYS_LIMIT &&
           size <= PHYS_LIMIT - address;
}
static uint32_t word(uint32_t address, unsigned offset) {
    const uint8_t *p = (const uint8_t *)(uintptr_t)address + offset;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int bootmem_capture(uint32_t address, struct boot_memory *out,
                    struct phys_range *reserved, unsigned *count) {
    if (!readable(address, 52)) return 0; /* Only the prefix we actually read. */
    uint32_t flags = word(address, 0);
    if (!(flags & (1u << 6))) return 0; /* Never infer RAM from mem_upper. */
    uint32_t map_length = word(address, 44), map_address = word(address, 48);
    if (!readable(map_address, map_length) ||
        !bootmem_parse((const void *)(uintptr_t)map_address, map_length, out)) return 0;
    *count = 0;
    reserved[(*count)++] = (struct phys_range){address, 52};
    reserved[(*count)++] = (struct phys_range){map_address, map_length};
    if (flags & (1u << 3)) {
        uint32_t modules = word(address, 20), table = word(address, 24);
        if (modules > BOOT_MAX_MODULES) return 0;
        if (modules && !readable(table, (uint64_t)modules * 16)) return 0;
        if (modules) reserved[(*count)++] = (struct phys_range){table, modules * 16};
        for (unsigned n = 0; n < modules; n++) {
            uint32_t start = word(table, n * 16), end = word(table, n * 16 + 4);
            if (end < start) return 0;
            reserved[(*count)++] = (struct phys_range){start, (uint64_t)end - start};
        }
    }
    /* Command strings, symbols and other unused fields are not retained/read.
     * Module payloads remain reserved even though this stage cannot execute them. */
    return 1;
}
