/* kernel.c - MiniOS entry point (milestone 2: GDT, IDT and CPU exceptions) */

#include <stdint.h>
#include <stddef.h>
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"

#define MULTIBOOT_MAGIC 0x2BADB002

static void ok(const char *msg) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("[ OK ] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(msg);
    vga_puts("\n");
}

void kernel_main(uint32_t magic, uint32_t mb_info) {
    (void)mb_info;

    vga_clear();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("MiniOS v0.2\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("-----------\n");

    if (magic != MULTIBOOT_MAGIC) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("Error: not booted by a Multiboot loader.\n");
        return;
    }

    ok("Booted via Multiboot");

    gdt_init();
    ok("GDT loaded (flat code and data segments)");

    idt_init();
    isr_install();
    ok("IDT loaded, CPU exception handlers installed");

    /* Self-test: raise a breakpoint exception on purpose. The handler prints
     * a message and returns, so execution continues on the next line. */
    __asm__ volatile ("int $3");
    ok("Returned from the breakpoint exception: the IDT works");

    /* Try a fatal exception: uncomment the next two lines, rebuild, and the
     * kernel will print "Divide by zero" and halt.
     *
     *   volatile int zero = 0;
     *   volatile int boom = 1 / zero;
     *   (void)boom;
     */

    vga_puts("\nNext: hardware interrupts (PIC + timer) and the keyboard.\n");
}
