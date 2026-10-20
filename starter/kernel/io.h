#ifndef STARTER_IO_H
#define STARTER_IO_H
#include <stdint.h>
static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port) : "memory");
}
static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}
static inline void io_wait(void) { out8(0x80, 0); }
#endif
