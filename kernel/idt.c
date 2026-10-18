/* idt.c - Interrupt Descriptor Table
 *
 * The IDT tells the CPU which function to run for each interrupt or
 * exception number (0-255). Entries 0-31 are reserved for CPU exceptions.
 */

#include "idt.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_entry) == 8, "an IDT entry is 8 bytes");

static struct idt_entry idt[256];
static struct idt_ptr   ip;

extern void idt_load(uint32_t idt_ptr_address);

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t flags) {
    idt[num].base_low  = (uint16_t)(handler & 0xFFFF);
    idt[num].base_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].selector  = selector;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

void idt_init(void) {
    ip.limit = (uint16_t)(sizeof(idt) - 1);
    ip.base  = (uint32_t)&idt;

    /* Start with every entry cleared (not present). */
    for (int i = 0; i < 256; i++)
        idt_set_gate((uint8_t)i, 0, 0, 0);

    idt_load((uint32_t)&ip);
}
