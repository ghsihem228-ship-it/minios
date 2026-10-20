#include "serial.h"
#include "interrupts.h"
_Static_assert(sizeof(void *) == 8, "This starter requires x86_64");
#ifndef TEST_FAULT
#define TEST_FAULT 0
#endif
void kernel_main(void) {
    serial_init();
    (void)serial_write("Hello\n");
    interrupts_init();
    (void)serial_write("IDT ready: 256 gates; TSS/IST ready\n");
    if (!interrupt_selftest()) {
        (void)serial_write("ERROR: interrupt self-test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)serial_write("PASS INT3/IRQ0: 64-bit GPRs, DF/IF, IRETQ; spurious IRQ7/15\n");
    timer_wait(3);
    (void)serial_write("PASS PIT: IRQ0 ticking at ~100 Hz\n");
    if (TEST_FAULT) interrupt_test_fault(TEST_FAULT);
    (void)serial_write("READY: interrupts enabled; idle\n");
    /* Timer IRQs wake HLT. No scheduler is introduced in this milestone. */
    for (;;) __asm__ volatile ("hlt" ::: "memory");
}
