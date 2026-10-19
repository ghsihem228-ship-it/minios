/* pmm.c - physical memory manager: a bitmap frame allocator.
 *
 * One bit per 4 KiB frame: 1 = used or reserved, 0 = free. The bitmap covers
 * the whole 32-bit address space (1M frames = 128 KiB) and lives in .bss. */

#include <stdint.h>
#include <stddef.h>
#include "pmm.h"
#include "vga.h"

#define MAX_FRAMES   (1024u * 1024u)          /* 4 GiB / 4 KiB */
#define BITMAP_WORDS (MAX_FRAMES / 32u)

#define MB_FLAG_MMAP (1u << 6)

struct mb_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed));

struct mb_mmap_entry {
    uint32_t size;      /* size of the rest of the entry (not counting this field) */
    uint64_t addr;
    uint64_t len;
    uint32_t type;      /* 1 = available RAM */
} __attribute__((packed));

extern char kernel_start[];
extern char kernel_end[];

static uint32_t bitmap[BITMAP_WORDS];
static uint32_t total_frames;     /* usable RAM frames found in the map */
static uint32_t free_frames;
static uint32_t search_hint;      /* word index where the next search starts */

static inline void frame_set(uint32_t f)   { bitmap[f / 32] |=  (1u << (f % 32)); }
static inline void frame_clear(uint32_t f) { bitmap[f / 32] &= ~(1u << (f % 32)); }
static inline int  frame_test(uint32_t f)  { return (bitmap[f / 32] >> (f % 32)) & 1u; }

/* Marks every frame touching [start, end) as used. Returns nothing: frames
 * already used are left alone, free ones are removed from the free count. */
static void reserve_range(uint64_t start, uint64_t end) {
    if (start >= 0x100000000ull)
        return;
    if (end > 0x100000000ull)
        end = 0x100000000ull;
    uint32_t first = (uint32_t)(start >> 12);
    uint32_t last  = (uint32_t)((end + PMM_FRAME_SIZE - 1) >> 12);
    for (uint32_t f = first; f < last; f++) {
        if (!frame_test(f)) {
            frame_set(f);
            free_frames--;
        }
    }
}

void pmm_init(uint32_t mb_info_addr) {
    const struct mb_info *mbi = (const struct mb_info *)mb_info_addr;

    /* Start with everything used, then free what the firmware says is RAM. */
    for (uint32_t i = 0; i < BITMAP_WORDS; i++)
        bitmap[i] = 0xFFFFFFFFu;
    total_frames = 0;
    free_frames = 0;

    if (!(mbi->flags & MB_FLAG_MMAP))
        return;     /* no memory map: leave everything reserved */

    uint32_t p = mbi->mmap_addr;
    uint32_t end = mbi->mmap_addr + mbi->mmap_length;
    while (p < end) {
        const struct mb_mmap_entry *e = (const struct mb_mmap_entry *)p;
        if (e->type == 1 && e->addr < 0x100000000ull) {
            uint64_t start = (e->addr + PMM_FRAME_SIZE - 1) & ~(uint64_t)(PMM_FRAME_SIZE - 1);
            uint64_t stop  = e->addr + e->len;
            if (stop > 0x100000000ull)
                stop = 0x100000000ull;
            stop &= ~(uint64_t)(PMM_FRAME_SIZE - 1);
            for (uint64_t a = start; a < stop; a += PMM_FRAME_SIZE) {
                frame_clear((uint32_t)(a >> 12));
                free_frames++;
                total_frames++;
            }
        }
        p += e->size + 4;
    }

    /* Carve out what must never be handed out. */
    reserve_range(0, 0x100000);                              /* BIOS, VGA, low memory */
    reserve_range((uint32_t)kernel_start, (uint32_t)kernel_end); /* kernel image + bitmap */
    reserve_range(mb_info_addr, mb_info_addr + sizeof(struct mb_info));
    reserve_range(mbi->mmap_addr, (uint64_t)mbi->mmap_addr + mbi->mmap_length);

    search_hint = 0;
}

uint32_t pmm_alloc_frame(void) {
    if (free_frames == 0)
        return 0;

    /* Next-fit: resume where the last search stopped, wrapping around once. */
    for (uint32_t n = 0; n < BITMAP_WORDS; n++) {
        uint32_t w = (search_hint + n) % BITMAP_WORDS;
        if (bitmap[w] == 0xFFFFFFFFu)
            continue;                       /* all 32 frames used: skip fast */
        for (uint32_t bit = 0; bit < 32; bit++) {
            if (!(bitmap[w] & (1u << bit))) {
                uint32_t f = w * 32 + bit;
                frame_set(f);
                free_frames--;
                search_hint = w;
                return f * PMM_FRAME_SIZE;
            }
        }
    }
    return 0;
}

void pmm_free_frame(uint32_t phys_addr) {
    uint32_t f = phys_addr / PMM_FRAME_SIZE;
    if (phys_addr % PMM_FRAME_SIZE != 0 || !frame_test(f))
        return;                             /* misaligned or already free */

    /* Refuse the obvious mistakes: the low 1 MiB and the kernel image. The
     * bitmap can not tell a hole in the memory map from an allocated frame,
     * so freeing an address that was never allocated is the caller's bug. */
    uint32_t addr = f * PMM_FRAME_SIZE;
    if (addr < 0x100000 || (addr >= (uint32_t)kernel_start && addr < (uint32_t)kernel_end))
        return;

    frame_clear(f);
    free_frames++;
    if (f / 32 < search_hint)
        search_hint = f / 32;
}

uint32_t pmm_total_frames(void) { return total_frames; }
uint32_t pmm_free_frames(void)  { return free_frames; }
uint32_t pmm_used_frames(void)  { return total_frames - free_frames; }

void pmm_print_map(uint32_t mb_info_addr) {
    const struct mb_info *mbi = (const struct mb_info *)mb_info_addr;
    if (!(mbi->flags & MB_FLAG_MMAP)) {
        vga_puts("  (no memory map from bootloader)\n");
        return;
    }
    uint32_t p = mbi->mmap_addr;
    uint32_t end = mbi->mmap_addr + mbi->mmap_length;
    while (p < end) {
        const struct mb_mmap_entry *e = (const struct mb_mmap_entry *)p;
        vga_puts("  ");
        vga_put_hex((uint32_t)e->addr);
        vga_puts(" - ");
        vga_put_hex((uint32_t)(e->addr + e->len - 1));
        vga_puts(e->type == 1 ? "  usable\n" : "  reserved\n");
        p += e->size + 4;
    }
}
