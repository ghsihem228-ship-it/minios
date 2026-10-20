#include "pmm.h"

/* One byte per frame: 256 KiB at the explicit 1-GiB physical ceiling.
 * More metadata than a bitmap, but ownership states prevent invalid frees. */
static uint8_t frames[PHYS_LIMIT / PAGE_SIZE];
static unsigned frame_count, hint;
static uint64_t free_frames;

static void range(uint64_t base, uint64_t length, int usable) {
    if (!length || base >= PHYS_LIMIT) return;
    uint64_t end = length > PHYS_LIMIT - base ? PHYS_LIMIT : base + length;
    /* Only fully usable frames become free; any reserved byte blocks a frame. */
    uint64_t first = usable ? (base + PAGE_SIZE - 1) / PAGE_SIZE : base / PAGE_SIZE;
    uint64_t last = usable ? end / PAGE_SIZE : (end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t n = first; n < last; n++) frames[n] = usable ? FRAME_FREE : FRAME_RESERVED;
}
int pmm_init(const struct boot_memory *map, const struct phys_range *reserved, unsigned count) {
    frame_count = hint = 0;
    free_frames = 0;
    for (unsigned n = 0; n < sizeof(frames); n++) frames[n] = FRAME_RESERVED;
    if (!map || !map->count || map->count > BOOT_MAX_RANGES ||
        count > BOOT_MAX_MODULES + 4 || (count && !reserved)) return 0;
    for (unsigned n = 0; n < map->count; n++) {
        struct memory_range r = map->ranges[n];
        if (r.length > UINT64_MAX - r.base) return 0;
        if (r.type == 1) range(r.base, r.length, 1);
    }
    for (unsigned n = 0; n < map->count; n++) {
        struct memory_range r = map->ranges[n];
        if (r.type != 1) range(r.base, r.length, 0);
    }
    range(0, 0x100000, 0); /* IVT, firmware, VGA and low bootloader structures. */
    for (unsigned n = 0; n < count; n++) {
        if (reserved[n].length > UINT64_MAX - reserved[n].base) return 0;
        range(reserved[n].base, reserved[n].length, 0);
    }
    for (unsigned n = 0; n < sizeof(frames); n++) if (frames[n] == FRAME_FREE) {
        free_frames++;
        frame_count = n + 1;
    }
    return free_frames != 0;
}
static uint64_t allocate(enum frame_state state) {
    if (!free_frames) return 0;
    for (unsigned visited = 0; visited < frame_count; visited++) {
        unsigned n = hint++;
        if (hint == frame_count) hint = 0;
        if (frames[n] != FRAME_FREE) continue;
        frames[n] = state;
        free_frames--;
        return (uint64_t)n * PAGE_SIZE;
    }
    return 0;
}
uint64_t pmm_alloc(void) { return allocate(FRAME_OWNED); }
uint64_t pmm_alloc_table(void) { return allocate(FRAME_TABLE); }
enum frame_state pmm_state(uint64_t physical) {
    if (physical % PAGE_SIZE || physical >= (uint64_t)frame_count * PAGE_SIZE) return FRAME_RESERVED;
    return frames[physical / PAGE_SIZE];
}
int pmm_free(uint64_t physical) {
    if (pmm_state(physical) != FRAME_OWNED) return 0;
    frames[physical / PAGE_SIZE] = FRAME_FREE;
    free_frames++;
    return 1;
}
int pmm_free_table(uint64_t physical) {
    if (pmm_state(physical) != FRAME_TABLE) return 0;
    frames[physical / PAGE_SIZE] = FRAME_FREE;
    free_frames++;
    return 1;
}
int pmm_set_mapped(uint64_t physical, int mapped) {
    if (pmm_state(physical) != (mapped ? FRAME_OWNED : FRAME_MAPPED)) return 0;
    frames[physical / PAGE_SIZE] = mapped ? FRAME_MAPPED : FRAME_OWNED;
    return 1;
}
uint64_t pmm_free_count(void) { return free_frames; }
unsigned pmm_frame_count(void) { return frame_count; }
