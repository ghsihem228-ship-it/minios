/* vga.c - minimal VGA text-mode driver (80x25) */

#include "vga.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)

static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = (VGA_LIGHT_GREY | (VGA_BLACK << 4));

static uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void scroll(void) {
    for (int y = 1; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
    for (int x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_entry(' ', current_color);
    cursor_y = VGA_HEIGHT - 1;
}

void vga_set_color(enum vga_color fg, enum vga_color bg) {
    current_color = (uint8_t)(fg | (bg << 4));
}

void vga_clear(void) {
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = make_entry(' ', current_color);
    cursor_x = 0;
    cursor_y = 0;
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = make_entry(c, current_color);
        if (++cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    if (cursor_y >= VGA_HEIGHT)
        scroll();
}

void vga_puts(const char *s) {
    while (*s)
        vga_putc(*s++);
}

/* Print a 32-bit value as 0xXXXXXXXX (needed to show addresses and error codes). */
void vga_put_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    vga_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putc(digits[(value >> shift) & 0xF]);
}
