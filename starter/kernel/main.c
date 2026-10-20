#include "serial.h"
_Static_assert(sizeof(void *) == 8, "This starter requires x86_64");
void kernel_main(void) {
    serial_init();
    (void)serial_write("Hello\n");
    /* No IDT yet: leave maskable interrupts disabled. Step 1 ends here. */
    for (;;) __asm__ volatile ("cli; hlt");
}
