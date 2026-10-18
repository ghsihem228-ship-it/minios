/* kernel.c - MiniOS entry point (milestone 3: hardware interrupts and keyboard) */

#include <stdint.h>
#include <stddef.h>
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "irq.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"

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
    vga_set_status_left("MiniOS v0.3");

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
    ok("Breakpoint exception handled, execution resumed");

    irq_install();
    pic_remap();
    ok("PIC remapped: hardware IRQs use vectors 32-47");

    timer_init(100);
    ok("Timer running at 100 Hz (see the uptime, top right)");

    keyboard_init();
    ok("Keyboard driver loaded");

    /* Turn interrupts on: from now on the timer and keyboard can interrupt us. */
    __asm__ volatile ("sti");

    /* Try a fatal exception: uncomment the next lines, rebuild, and the
     * kernel will print "Divide by zero" and halt.
     *
     *   volatile int zero = 0;
     *   volatile int boom = 1 / zero;
     *   (void)boom;
     */

    vga_puts("\nInterrupts are on. Type something (Backspace and Shift work):\n> ");

    for (;;) {
        char c = keyboard_getchar();
        vga_putc(c);
        if (c == '\n')
            vga_puts("> ");
    }
}
