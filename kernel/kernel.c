/* kernel.c - MiniOS entry point: memory management and interactive shell */

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
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "shell.h"
#include "vfs.h"
#include "programs.h"
#include "syscall.h"
#include "task.h"
#include "elf.h"
#include "net.h"

#define MULTIBOOT_MAGIC 0x2BADB002

static void ok(const char *msg) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("[ OK ] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(msg);
    vga_puts("\n");
}

static void fail(const char *msg) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_puts("[FAIL] "); vga_puts(msg); vga_puts("\nSystem halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

void kernel_main(uint32_t magic, uint32_t mb_info) {
    vga_clear();
    vga_set_status_left("MiniOS v0.8");

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

    pmm_init(mb_info);
    ok("Physical memory manager initialised");
    vga_puts("      Memory map:\n");
    pmm_print_map(mb_info);
    vga_puts("      RAM: ");
    vga_put_dec(pmm_total_frames() * (PMM_FRAME_SIZE / 1024) / 1024);
    vga_puts(" MiB usable, ");
    vga_put_dec(pmm_free_frames());
    vga_puts(" frames free, ");
    vga_put_dec(pmm_used_frames());
    vga_puts(" reserved\n");

    /* Self-test: allocate, free, and check the allocator reuses the frame. */
    uint32_t a = pmm_alloc_frame();
    uint32_t b = pmm_alloc_frame();
    uint32_t before = pmm_free_frames();
    pmm_free_frame(a);
    uint32_t c = pmm_alloc_frame();
    if (a && b && a != b && c == a && pmm_free_frames() == before) {
        ok("Frame allocator self-test passed");
    } else {
        fail("Frame allocator self-test");
    }
    pmm_free_frame(b);
    pmm_free_frame(c);

    if (!paging_init()) fail("Paging initialisation");
    ok("Paging enabled; NULL page unmapped");
    if (!paging_self_test()) fail("Paging self-test");
    ok("Paging self-test passed");
    if (!heap_init()) fail("Heap initialisation");
    if (!heap_self_test()) fail("Heap self-test");
    ok("Heap self-test passed");

    fs_init();
    if (!programs_install() || !fs_self_test()) fail("RAMFS self-test");
    ok("RAMFS self-test passed; ELF programs installed");
    if (!elf_self_test()) fail("ELF loader self-test");
    ok("ELF loader self-test passed");
    syscall_install();
    task_init();
    ok("Ring 3 / TSS / int 0x80 / preemptive scheduler ready");
    net_init();

    /* Turn interrupts on: from now on the timer and keyboard can interrupt us. */
    __asm__ volatile ("sti");

    shell_run(mb_info);
}
