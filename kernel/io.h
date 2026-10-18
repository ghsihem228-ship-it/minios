#ifndef IO_H
#define IO_H

#include <stdint.h>

/* Port-mapped I/O: how the CPU talks to the PIC, timer, keyboard, VGA cursor... */

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Tiny delay: writing to an unused port takes roughly a microsecond. */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif
