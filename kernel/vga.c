/* vga.c - VGA text-mode driver (80x25) with a status bar and a hardware cursor
 *
 * Row 0 is a status bar (left text, right text). Scrolling and typing only
 * affect rows 1-24, so the status bar can be refreshed from an interrupt
 * handler without disturbing the text.
 */

#include "vga.h"
#include "io.h"

#define VGA_WIDTH       80
#define VGA_HEIGHT      25
#define VGA_STATUS_ROWS 1
#define VGA_MEMORY      ((volatile uint16_t *)0xB8000)
#define STATUS_COLOR    ((uint8_t)(VGA_WHITE | (VGA_BLUE << 4)))

static int cursor_x = 0;
static int cursor_y = VGA_STATUS_ROWS;
static uint8_t current_color = (VGA_LIGHT_GREY | (VGA_BLACK << 4));

static char status_left[VGA_WIDTH];
static char status_right[VGA_WIDTH];

static uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

static int str_len(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void copy_str(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void draw_status(void) {
    for (int x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[x] = make_entry(' ', STATUS_COLOR);

    for (int i = 0; status_left[i] && i + 1 < VGA_WIDTH; i++)
        VGA_MEMORY[i + 1] = make_entry(status_left[i], STATUS_COLOR);

    int len = str_len(status_right);
    int start = VGA_WIDTH - 1 - len;
    for (int i = 0; i < len && start + i >= 0; i++)
        VGA_MEMORY[start + i] = make_entry(status_right[i], STATUS_COLOR);
}

/* Move the blinking hardware cursor to the current position. */
static void update_cursor(void) {
    uint16_t pos = (uint16_t)(cursor_y * VGA_WIDTH + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    for (int y = VGA_STATUS_ROWS + 1; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
    for (int x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_entry(' ', current_color);
    cursor_y = VGA_HEIGHT - 1;
}

void vga_set_color(enum vga_color fg, enum vga_color bg) {
    current_color = (uint8_t)(fg | (bg << 4));
}

void vga_set_status_left(const char *s) {
    copy_str(status_left, s, VGA_WIDTH);
    draw_status();
}

void vga_set_status_right(const char *s) {
    copy_str(status_right, s, VGA_WIDTH);
    draw_status();
}

void vga_clear(void) {
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = make_entry(' ', current_color);
    cursor_x = 0;
    cursor_y = VGA_STATUS_ROWS;
    draw_status();
    update_cursor();
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\t') {
        for (int i = 0; i < 4; i++)
            vga_putc(' ');
        return;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > VGA_STATUS_ROWS) {
            cursor_y--;
            cursor_x = VGA_WIDTH - 1;
        }
        VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = make_entry(' ', current_color);
    } else {
        VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = make_entry(c, current_color);
        if (++cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= VGA_HEIGHT)
        scroll();
    update_cursor();
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
