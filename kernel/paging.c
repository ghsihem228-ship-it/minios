/* Bootstrap paging: low identity map + a separate, demand-backed heap. */
#include "paging.h"
#include "pmm.h"

#define PRESENT 1u
#define WRITE 2u
#define USER 4u
#define FRAME_MASK 0xFFFFF000u

static uint32_t directory[1024] __attribute__((aligned(PAGE_SIZE)));
static uint32_t low_table[1024] __attribute__((aligned(PAGE_SIZE)));
static uint32_t heap_table[1024] __attribute__((aligned(PAGE_SIZE)));
struct address_space {
    uint32_t directory[1024];
    uint32_t table[1024];
} __attribute__((aligned(PAGE_SIZE)));
static struct address_space spaces[MAX_USER_SPACES];
extern char kernel_end[];

int paging_enabled(void) {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & (1u << 31)) != 0;
}

int paging_init(void) {
    if (paging_enabled())
        return 0;
    if ((uint32_t)kernel_end > 4u * 1024u * 1024u)
        return 0; /* kernel, stack and tables must fit in the identity map */
    for (uint32_t i = 0; i < 1024; i++) {
        directory[i] = 0;
        heap_table[i] = 0;
        low_table[i] = i ? (i * PAGE_SIZE) | PRESENT | WRITE : 0;
    }
    directory[0] = (uint32_t)low_table | PRESENT | WRITE;
    directory[HEAP_BASE >> 22] = (uint32_t)heap_table | PRESENT | WRITE;
    __asm__ volatile ("mov %0, %%cr3" :: "r"((uint32_t)directory) : "memory");
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1u << 31) | (1u << 16) | (1u << 2) | (1u << 3);
    /* PG, WP, EM, TS: no unsaved x87/MMX state may cross processes. */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
    return paging_enabled();
}

static int heap_page(uint32_t virt) {
    return virt >= HEAP_BASE && virt - HEAP_BASE < 4u * 1024u * 1024u
        && !(virt & (PAGE_SIZE - 1));
}

int paging_map_page(uint32_t virt, uint32_t phys) {
    if (!paging_enabled() || !heap_page(virt) || !phys ||
        (phys & (PAGE_SIZE - 1)))
        return 0;
    uint32_t *entry = &heap_table[(virt - HEAP_BASE) / PAGE_SIZE];
    if (*entry & PRESENT)
        return 0;
    *entry = phys | PRESENT | WRITE;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    return 1;
}

int paging_unmap_page(uint32_t virt) {
    if (!paging_enabled() || !heap_page(virt))
        return 0;
    uint32_t *entry = &heap_table[(virt - HEAP_BASE) / PAGE_SIZE];
    if (!(*entry & PRESENT))
        return 0;
    *entry = 0;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    return 1;
}

int paging_translate(uint32_t virt, uint32_t *phys) {
    if (!paging_enabled() || !phys)
        return 0;
    uint32_t *table;
    if ((virt >> 22) == 0)
        table = low_table;
    else if ((virt >> 22) == (HEAP_BASE >> 22))
        table = heap_table;
    else
        return 0;
    uint32_t entry = table[(virt >> 12) & 1023];
    if (!(entry & PRESENT))
        return 0;
    *phys = (entry & FRAME_MASK) | (virt & (PAGE_SIZE - 1));
    return 1;
}

int paging_self_test(void) {
    const uint32_t scratch = HEAP_BASE + HEAP_MAX_BYTES;
    uint32_t before = pmm_free_frames(), phys = 0;
    uint32_t a = pmm_alloc_frame(), b = pmm_alloc_frame();
    int mapped_a = 0, mapped_b = 0, ok = 0;
    volatile uint32_t *x = (volatile uint32_t *)scratch;
    volatile uint32_t *y = (volatile uint32_t *)(scratch + PAGE_SIZE);
    if (!a || !b || a == b || paging_translate(0, &phys))
        goto done;
    mapped_a = paging_map_page(scratch, a);
    mapped_b = paging_map_page(scratch + PAGE_SIZE, b);
    if (!mapped_a || !mapped_b)
        goto done;
    x[0] = 0x12345678; x[1023] = 0xAABBCCDD;
    y[0] = 0x87654321; y[1023] = 0xDDCCBBAA;
    if (x[0] != 0x12345678 || x[1023] != 0xAABBCCDD ||
        y[0] != 0x87654321 || y[1023] != 0xDDCCBBAA ||
        !paging_translate(scratch + 123, &phys) || phys != a + 123 ||
        paging_map_page(scratch, b))
        goto done;
    paging_unmap_page(scratch);
    mapped_a = 0;
    if (paging_translate(scratch, &phys))
        goto done;
    mapped_a = paging_map_page(scratch, b);
    if (!mapped_a || x[0] != y[0] || x[1023] != y[1023])
        goto done; /* remapping must invalidate the old TLB entry */
    x[0] = 42;
    ok = y[0] == 42;
done:
    if (mapped_a) paging_unmap_page(scratch);
    if (mapped_b) paging_unmap_page(scratch + PAGE_SIZE);
    if (a) pmm_free_frame(a);
    if (b) pmm_free_frame(b);
    return ok && pmm_free_frames() == before;
}

void paging_space_init(unsigned slot) {
    if (slot >= MAX_USER_SPACES) return;
    for (unsigned i = 0; i < 1024; i++) {
        spaces[slot].directory[i] = directory[i];
        spaces[slot].table[i] = 0;
    }
    spaces[slot].directory[USER_BASE >> 22] =
        (uint32_t)spaces[slot].table | PRESENT | WRITE | USER;
}

void paging_space_activate(int slot) {
    uint32_t cr3 = (uint32_t)directory;
    if (slot >= 0 && slot < MAX_USER_SPACES)
        cr3 = (uint32_t)spaces[slot].directory;
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

void paging_space_destroy(unsigned slot) {
    if (slot >= MAX_USER_SPACES) return;
    /* Only reap from the kernel task, after switching away from this CR3. */
    for (unsigned i = 0; i < 1024; i++) {
        uint32_t entry = spaces[slot].table[i];
        if (entry & PRESENT) pmm_free_frame(entry & FRAME_MASK);
        spaces[slot].table[i] = 0;
    }
}

int paging_user_map(unsigned slot, uint32_t virt, int writable) {
    if (slot >= MAX_USER_SPACES || virt < USER_BASE || virt >= USER_TOP ||
        (virt & (PAGE_SIZE - 1))) return 0;
    uint32_t *entry = &spaces[slot].table[(virt - USER_BASE) / PAGE_SIZE];
    if (*entry & PRESENT) return 0;
    uint32_t frame = pmm_alloc_frame();
    if (!frame) return 0;
    const uint32_t scratch = HEAP_BASE + HEAP_MAX_BYTES;
    if (!paging_map_page(scratch, frame)) {
        pmm_free_frame(frame); return 0;
    }
    volatile uint32_t *words = (volatile uint32_t *)scratch;
    for (unsigned i = 0; i < 1024; i++) words[i] = 0;
    paging_unmap_page(scratch);
    *entry = frame | PRESENT | USER | (writable ? WRITE : 0);
    return 1;
}

int paging_space_write(unsigned slot, uint32_t virt, const void *src, uint32_t len) {
    if (!paging_user_range(slot, virt, len, 0)) return 0;
    const uint8_t *from = src;
    const uint32_t scratch = HEAP_BASE + HEAP_MAX_BYTES;
    while (len) {
        uint32_t offset = virt & (PAGE_SIZE - 1);
        uint32_t bytes = PAGE_SIZE - offset;
        if (bytes > len) bytes = len;
        uint32_t entry = spaces[slot].table[(virt - USER_BASE) / PAGE_SIZE];
        if (!paging_map_page(scratch, entry & FRAME_MASK)) return 0;
        uint8_t *to = (uint8_t *)(scratch + offset);
        for (uint32_t i = 0; i < bytes; i++) to[i] = from[i];
        paging_unmap_page(scratch);
        virt += bytes; from += bytes; len -= bytes;
    }
    return 1;
}

int paging_user_range(unsigned slot, uint32_t addr, uint32_t len, int write) {
    if (slot >= MAX_USER_SPACES || addr < USER_BASE || addr >= USER_TOP ||
        len > USER_TOP - addr) return 0;
    if (!len) return 1;
    uint32_t last = (addr + len - 1 - USER_BASE) / PAGE_SIZE;
    for (uint32_t i = (addr - USER_BASE) / PAGE_SIZE; i <= last; i++) {
        uint32_t e = spaces[slot].table[i];
        if (!(e & PRESENT) || !(e & USER) || (write && !(e & WRITE))) return 0;
    }
    return 1;
}
