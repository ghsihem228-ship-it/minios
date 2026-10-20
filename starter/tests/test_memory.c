/* Exercise the exact freestanding decoder/allocator on the host with sanitizers. */
#include "bootmem.h"
#include "pmm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t seed = 0x691731a5;
static uint32_t random32(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned n = 0; n < 4; n++) p[n] = (uint8_t)(v >> (n * 8));
}
static void record(uint8_t *p, uint32_t size, uint64_t base, uint64_t length, uint32_t type) {
    put32(p, size); put32(p + 4, base); put32(p + 8, base >> 32);
    put32(p + 12, length); put32(p + 16, length >> 32); put32(p + 20, type);
}
static void decoder(void) {
    uint8_t data[4096] = {0};
    struct boot_memory map;
    record(data, 24, 0x100003, 0x987654, 1); /* Extension bytes must be skipped. */
    record(data + 28, 20, 0x800000, 4096, 2);
    assert(bootmem_parse(data, 52, &map) && map.count == 2);
    assert(map.ranges[0].base == 0x100003 && map.ranges[1].type == 2);
    assert(!bootmem_parse(data, 51, &map) && !map.count);
    put32(data, 19);
    assert(!bootmem_parse(data, 52, &map) && !map.count);
    record(data, 20, UINT64_MAX - 4, 8, 1);
    assert(!bootmem_parse(data, 24, &map) && !map.count);
    record(data, UINT32_MAX, 0, 1, 1);
    assert(!bootmem_parse(data, 24, &map));
    assert(!bootmem_parse(NULL, 24, &map) && !map.count);
    assert(!bootmem_parse(data, 65537, &map));
    for (unsigned n = 0; n < 129; n++) record(data + n * 24, 20, n * 4096, 4096, 1);
    assert(!bootmem_parse(data, 129 * 24, &map) && !map.count);
    assert(bootmem_parse(data, 128 * 24, &map) && map.count == 128);
    puts("PASS host: variable-size mmap, truncation, overflow, range limits");
}
static void fuzz_decoder(void) {
    for (unsigned iteration = 0; iteration < 100000; iteration++) {
        uint8_t bytes[512];
        for (unsigned n = 0; n < sizeof(bytes); n++) bytes[n] = random32();
        size_t length = random32() % (sizeof(bytes) + 1);
        if (iteration & 1) {
            size_t cursor = 0;
            unsigned records = random32() % 12 + 1;
            for (unsigned n = 0; n < records; n++) {
                uint32_t size = 20 + 4 * (random32() % 3);
                uint64_t base = ((uint64_t)random32() << 32) | random32();
                uint64_t span = random32();
                record(bytes + cursor, size, base, span, random32() % 6);
                cursor += size + 4;
            }
            length = cursor;
            if (iteration & 2) bytes[random32() % length] ^= random32();
            if (iteration & 4) length = random32() % (length + 1);
        }
        struct { uint64_t before; struct boot_memory map; uint64_t after; } out;
        out.before = out.after = UINT64_C(0xfedcba9876543210);
        /* Exact allocation makes ASan notice reads beyond the supplied blob. */
        uint8_t exact[length ? length : 1];
        memcpy(exact, bytes, length);
        int valid = bootmem_parse(exact, length, &out.map);
        assert(out.before == UINT64_C(0xfedcba9876543210) && out.before == out.after);
        assert(out.map.count <= BOOT_MAX_RANGES);
        if (valid) {
            assert(out.map.count);
            for (unsigned n = 0; n < out.map.count; n++) {
                struct memory_range r = out.map.ranges[n];
                assert(r.length && r.length <= UINT64_MAX - r.base);
            }
        } else assert(!out.map.count);
    }
    puts("PASS host: 100000 mmap fuzz cases under ASan/UBSan");
}
#define MODEL_FRAMES (64 * 1024 * 1024 / 4096)
static uint8_t model[MODEL_FRAMES];
static int overlaps(uint64_t start, uint64_t end, uint64_t base, uint64_t length) {
    return length && base < end && start < base + length;
}
static void allocator(void) {
    const struct boot_memory map = {.count = 5, .ranges = {
        {0, 64 * 1024 * 1024, 1}, {0x220123, 19000, 2},
        {0x220000, 0x20000, 1}, /* Available overlap must not override reserved. */
        {0x100000000ull, 0x10000000, 1}, {0x555123, 1, 99}
    }};
    const struct phys_range reserved[] = {{0x100000, 0x80001}, {0x500123, 4096}};
    assert(pmm_init(&map, reserved, 2));
    uint64_t free = 0;
    for (unsigned n = 0; n < MODEL_FRAMES; n++) {
        uint64_t start = n * PAGE_SIZE, end = start + PAGE_SIZE;
        int usable = 0;
        for (unsigned j = 0; j < map.count; j++) {
            struct memory_range r = map.ranges[j];
            if (r.type == 1 && start >= r.base && end <= r.base + r.length) usable = 1;
        }
        for (unsigned j = 0; j < map.count; j++) {
            struct memory_range r = map.ranges[j];
            if (r.type != 1 && overlaps(start, end, r.base, r.length)) usable = 0;
        }
        for (unsigned j = 0; j < 2; j++)
            if (overlaps(start, end, reserved[j].base, reserved[j].length)) usable = 0;
        if (start < 0x100000) usable = 0;
        model[n] = usable ? FRAME_FREE : FRAME_RESERVED;
        assert(pmm_state(start) == model[n]);
        free += usable;
    }
    assert(pmm_free_count() == free);
    assert(pmm_state(PHYS_LIMIT) == FRAME_RESERVED && !pmm_free(PHYS_LIMIT));
    for (unsigned iteration = 0; iteration < 50000; iteration++) {
        unsigned op = random32() % 6, n = random32() % MODEL_FRAMES;
        uint64_t pa = n * PAGE_SIZE;
        if (op < 3) {
            int table = op == 2;
            uint64_t result = table ? pmm_alloc_table() : pmm_alloc();
            if (!free) assert(!result);
            else {
                assert(result && result % PAGE_SIZE == 0 && result / PAGE_SIZE < MODEL_FRAMES);
                n = result / PAGE_SIZE;
                assert(model[n] == FRAME_FREE);
                model[n] = table ? FRAME_TABLE : FRAME_OWNED;
                free--;
            }
        } else if (op == 3) {
            int expected = model[n] == FRAME_OWNED;
            assert(pmm_free(pa) == expected);
            if (expected) { model[n] = FRAME_FREE; free++; }
        } else {
            int mapped = op == 4;
            int expected = model[n] == (mapped ? FRAME_OWNED : FRAME_MAPPED);
            assert(pmm_set_mapped(pa, mapped) == expected);
            if (expected) model[n] = mapped ? FRAME_MAPPED : FRAME_OWNED;
        }
        assert(pmm_free_count() == free);
        assert(!pmm_free(pa + 1));
    }
    uint64_t remaining = free, allocated = 0, address;
    while ((address = pmm_alloc())) {
        unsigned n = address / PAGE_SIZE;
        assert(model[n] == FRAME_FREE);
        model[n] = FRAME_OWNED;
        allocated++;
    }
    assert(allocated == remaining && !pmm_alloc_table() && !pmm_free_count());
    for (unsigned n = 0; n < MODEL_FRAMES; n++) {
        assert(pmm_state(n * PAGE_SIZE) == model[n]);
        if (model[n] == FRAME_MAPPED) assert(pmm_set_mapped(n * PAGE_SIZE, 0));
        if (model[n] == FRAME_OWNED || model[n] == FRAME_MAPPED) {
            assert(pmm_free(n * PAGE_SIZE));
            assert(!pmm_free(n * PAGE_SIZE));
        } else assert(!pmm_free(n * PAGE_SIZE));
    }
    /* High-memory clipping and partial edge frames without integer wrapping. */
    struct boot_memory edge = {.count = 2, .ranges = {
        {PHYS_LIMIT - 8193, 0x100000000ull, 1}, {PHYS_LIMIT - 1, 1, 2}
    }};
    assert(pmm_init(&edge, NULL, 0));
    assert(pmm_free_count() == 1 && pmm_alloc() == PHYS_LIMIT - 8192 && !pmm_alloc());
    edge.ranges[0] = (struct memory_range){UINT64_MAX - 1, 9, 1};
    assert(!pmm_init(&edge, NULL, 0) && !pmm_alloc());
    puts("PASS host: PMM independent model, 50000 ownership operations, exhaustion, reserved overlaps, 1-GiB clipping");
}
int main(void) {
    decoder(); fuzz_decoder(); allocator();
    return 0;
}
