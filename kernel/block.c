#include "block.h"
#include "io.h"

#define DATA 0x1F0
#define STATUS 0x1F7
#define CONTROL 0x3F6
#define BUSY 0x80
#define DRQ 0x08
#define FAIL 0x21
#define POLLS 1000000u
static uint32_t sectors;

static void delay(void) {
    for (unsigned i = 0; i < 4; i++) (void)inb(CONTROL);
}
static int wait(int data) {
    for (uint32_t i = 0; i < POLLS; i++) {
        uint8_t s = inb(STATUS);
        if (!s || s == 0xFF) return -19;
        if (s & BUSY) continue;
        if (s & FAIL) return -5;
        if (!data || (s & DRQ)) return 0;
    }
    return -110;
}
static int select(uint32_t lba) {
    if (!sectors || lba >= sectors) return -22;
    for (uint32_t i = 0; i < POLLS; i++) {
        if (!(inb(STATUS) & BUSY)) {
            outb(0x1F6, (uint8_t)(0xE0 | (lba >> 24)));
            delay();
            outb(0x1F2, 1);
            outb(0x1F3, (uint8_t)lba);
            outb(0x1F4, (uint8_t)(lba >> 8));
            outb(0x1F5, (uint8_t)(lba >> 16));
            return 0;
        }
    }
    return -110;
}
int block_init(void) {
    uint16_t identify[256];
    sectors = 0;
    outb(CONTROL, 2); /* nIEN: polling, no ATA IRQs */
    outb(0x1F6, 0xA0); delay();
    outb(0x1F2, 0); outb(0x1F3, 0); outb(0x1F4, 0); outb(0x1F5, 0);
    outb(STATUS, 0xEC);
    if (!inb(STATUS) || inb(STATUS) == 0xFF) return -19;
    int result = wait(1);
    if (result || inb(0x1F4) || inb(0x1F5)) return result ? result : -95;
    for (unsigned i = 0; i < 256; i++)
        __asm__ volatile ("inw %1, %0" : "=a"(identify[i]) : "Nd"((uint16_t)DATA));
    if (!(identify[49] & (1u << 9)) || (identify[0] & 0x8000)) return -95;
    sectors = identify[60] | ((uint32_t)identify[61] << 16);
    if (sectors > 0x10000000u) sectors = 0x10000000u;
    return sectors ? 0 : -19;
}
uint32_t block_sectors(void) { return sectors; }
int block_read(uint32_t lba, void *sector) {
    int result = select(lba);
    if (result) return result;
    outb(STATUS, 0x20); delay();
    result = wait(1);
    if (result) return result;
    uint16_t *words = sector;
    for (unsigned i = 0; i < 256; i++)
        __asm__ volatile ("inw %1, %0" : "=a"(words[i]) : "Nd"((uint16_t)DATA));
    delay(); return wait(0);
}
int block_write(uint32_t lba, const void *sector) {
    int result = select(lba);
    if (result) return result;
    outb(STATUS, 0x30); delay();
    result = wait(1);
    if (result) return result;
    const uint16_t *words = sector;
    for (unsigned i = 0; i < 256; i++)
        __asm__ volatile ("outw %0, %1" :: "a"(words[i]), "Nd"((uint16_t)DATA));
    delay(); return wait(0);
}
int block_flush(void) {
    if (!sectors) return -19;
    outb(STATUS, 0xE7); delay(); return wait(0);
}
