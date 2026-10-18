/* gdt.c - Global Descriptor Table (flat memory model)
 *
 * Protected mode still requires segments. We use the simplest setup:
 * three entries that all cover the whole 4 GiB address space.
 *
 *   0: null descriptor (required by the CPU)
 *   1: kernel code segment (selector 0x08)
 *   2: kernel data segment (selector 0x10)
 *
 * User-mode segments and a TSS are added later, when we run user programs.
 */

#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct gdt_entry) == 8, "a GDT entry is 8 bytes");

static struct gdt_entry gdt[3];
static struct gdt_ptr   gp;

extern void gdt_flush(uint32_t gdt_ptr_address);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[num].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high   = (uint8_t)((base >> 24) & 0xFF);

    gdt[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[num].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[num].access      = access;
}

void gdt_init(void) {
    gp.limit = (uint16_t)(sizeof(gdt) - 1);
    gp.base  = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                 /* null */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);  /* kernel code: ring 0, executable */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);  /* kernel data: ring 0, writable */

    gdt_flush((uint32_t)&gp);
}
