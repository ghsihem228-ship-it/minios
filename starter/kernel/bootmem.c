#include "bootmem.h"

/* Byte decoding avoids alignment assumptions and packed/aliased loads. */
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p) {
    return u32(p) | (uint64_t)u32(p + 4) << 32;
}
int bootmem_parse(const void *bytes, size_t length, struct boot_memory *out) {
    if (!out) return 0;
    out->count = 0;
    if (!bytes || !length || length > 65536) return 0;
    const uint8_t *p = bytes;
    size_t offset = 0;
    while (offset < length) {
        if (length - offset < 4) goto invalid;
        uint32_t size = u32(p + offset);
        if (size < 20 || size > length - offset - 4) goto invalid;
        uint64_t base = u64(p + offset + 4), span = u64(p + offset + 12);
        uint32_t type = u32(p + offset + 20);
        if (span > UINT64_MAX - base) goto invalid;
        if (span) {
            if (out->count == BOOT_MAX_RANGES) goto invalid;
            out->ranges[out->count++] = (struct memory_range){base, span, type};
        }
        offset += (size_t)size + 4;
    }
    if (out->count) return 1;
invalid:
    out->count = 0; /* No partially decoded map may be used for allocation. */
    return 0;
}
