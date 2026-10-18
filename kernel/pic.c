/* pic.c - 8259 Programmable Interrupt Controller
 *
 * Hardware devices (timer, keyboard, disk...) raise IRQ lines 0-15. By default
 * the PICs map those to interrupt vectors 8-15, which collide with CPU
 * exceptions (for example vector 8 is the double fault). We remap them to
 * vectors 32-47 and mask every line until a driver asks for it.
 */

#include "pic.h"
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI   0x20

void pic_remap(void) {
    outb(PIC1_CMD, 0x11); io_wait();     /* start initialisation (cascade mode) */
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();    /* master vector offset: 32 */
    outb(PIC2_DATA, 0x28); io_wait();    /* slave vector offset: 40 */
    outb(PIC1_DATA, 0x04); io_wait();    /* master: slave is on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();    /* slave: cascade identity 2 */
    outb(PIC1_DATA, 0x01); io_wait();    /* 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFF);               /* mask every IRQ for now */
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(irq & 7);
    outb(port, (uint8_t)(inb(port) & ~(1 << bit)));

    /* IRQs 8-15 reach the CPU through IRQ2 on the master, so open it too. */
    if (irq >= 8)
        outb(PIC1_DATA, (uint8_t)(inb(PIC1_DATA) & ~(1 << 2)));
}

/* Tell the PIC we finished handling an IRQ, otherwise it sends no more. */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}
