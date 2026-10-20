#include "serial.h"
#include <stdint.h>
#define COM1 0x3F8u
#define TX_POLLS 100000u
static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port) : "memory");
}
static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}
void serial_init(void) {
    out8(COM1 + 1, 0);     /* UART interrupts disabled. */
    out8(COM1 + 3, 0x80);  /* DLAB: access the baud divisor. */
    out8(COM1, 1);         /* 115200 / 1 baud. */
    out8(COM1 + 1, 0);
    out8(COM1 + 3, 3);     /* DLAB off, 8N1. */
    out8(COM1 + 2, 0xC7);  /* Enable and clear FIFOs. */
    out8(COM1 + 4, 3);     /* Assert DTR/RTS. */
}
static int put_byte(uint8_t value) {
    for (unsigned n = 0; n < TX_POLLS; n++) {
        if (in8(COM1 + 5) & 0x20) {
            out8(COM1, value);
            return 1;
        }
    }
    return 0;
}
int serial_write(const char *text) {
    for (; *text; text++) {
        if (*text == '\n' && !put_byte('\r')) return 0;
        if (!put_byte((uint8_t)*text)) return 0;
    }
    return 1;
}
