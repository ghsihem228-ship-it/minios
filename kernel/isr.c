/* isr.c - CPU exception handling (interrupts 0-31) */

#include "isr.h"
#include "idt.h"
#include "gdt.h"
#include "vga.h"
#include "irq.h"

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

static void (*const isr_stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

static const char *exception_names[32] = {
    "Divide by zero",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 floating-point exception",
    "Alignment check",
    "Machine check",
    "SIMD floating-point exception",
    "Virtualization exception",
    "Control protection exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection exception",
    "VMM communication exception",
    "Security exception",
    "Reserved",
};

void isr_install(void) {
    for (int i = 0; i < 32; i++) {
        /* 0x8E = present, ring 0, 32-bit interrupt gate */
        idt_set_gate((uint8_t)i, (uint32_t)isr_stubs[i], GDT_KERNEL_CODE, 0x8E);
    }
}

void isr_handler(struct regs *r) {
    /* Vectors 32-47 are hardware interrupts (IRQ 0-15), not CPU exceptions. */
    if (r->int_no >= 32 && r->int_no < 48) {
        irq_dispatch(r);
        return;
    }

    /* The breakpoint exception is harmless: report it and keep running.
     * It lets us test that the whole IDT path works. */
    if (r->int_no == 3) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_puts("[breakpoint handled, eip=");
        vga_put_hex(r->eip);
        vga_puts("]\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    /* Any other exception is fatal for now: print details and halt. */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n*** CPU EXCEPTION ***\n");
    vga_puts(r->int_no < 32 ? exception_names[r->int_no] : "Unknown");
    vga_puts("\nint=");
    vga_put_hex(r->int_no);
    vga_puts(" err=");
    vga_put_hex(r->err_code);
    vga_puts(" eip=");
    vga_put_hex(r->eip);
    vga_puts("\nSystem halted.\n");

    for (;;)
        __asm__ volatile ("cli; hlt");
}
