#include "console.h"
#include "io.h"
#include <stdint.h>
#define WIDTH 80u
#define HEIGHT 25u
static volatile uint16_t *const screen = (volatile uint16_t *)(uintptr_t)0xb8000;
static unsigned row, column, enabled, shifts, extended, caps;
static void cursor(void) {
    unsigned position = row * WIDTH + column;
    out8(0x3d4, 14); out8(0x3d5, position >> 8);
    out8(0x3d4, 15); out8(0x3d5, position & 255);
}
void console_clear(void) {
    for (unsigned i = 0; i < WIDTH * HEIGHT; i++) screen[i] = 0x0720;
    row = column = 0; cursor();
}
void console_init(void) { console_clear(); enabled = 1; }
void console_putc(char c) {
    if (!enabled) return;
    if (c == '\n') { column = 0; row++; }
    else if (c == '\r') column = 0;
    else if (c == '\b') {
        if (column) column--;
        else if (row) { row--; column = WIDTH - 1; }
        screen[row * WIDTH + column] = 0x0720;
    } else if ((unsigned char)c >= 32 && (unsigned char)c < 127) {
        screen[row * WIDTH + column] = 0x0700 | (unsigned char)c;
        if (++column == WIDTH) { column = 0; row++; }
    }
    if (row == HEIGHT) {
        for (unsigned i = 0; i < WIDTH * (HEIGHT - 1); i++) screen[i] = screen[i + WIDTH];
        for (unsigned i = WIDTH * (HEIGHT - 1); i < WIDTH * HEIGHT; i++) screen[i] = 0x0720;
        row = HEIGHT - 1;
    }
    cursor();
}
/* PS/2 set-1 US layout. No keyboard IRQ is unmasked: PIT wakes the poll loop.
 * Extended keys, mouse data and controller error bytes are deliberately ignored. */
static const char normal[128] = {
    [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
    [12]='-',[13]='=',[14]='\b',[15]='\t',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',
    [21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\n',
    [30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',
    [39]=';',[40]='\'',[41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',
    [48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[57]=' '
};
static const char shifted[128] = {
    [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',
    [12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]='"',[41]='~',[43]='|',
    [51]='<',[52]='>',[53]='?'
};
int console_input(void) {
    /* Prefer serial input for headless sessions; UART interrupts stay disabled. */
    if (in8(0x3fd) & 1) {
        unsigned char c = in8(0x3f8);
        if (c == '\r') return '\n';
        if (c == 127) return '\b';
        if (c == '\n' || c == '\b' || (c >= 32 && c < 127)) return c;
    }
    for (unsigned attempt = 0; attempt < 16; attempt++) {
        uint8_t status = in8(0x64);
        if (!(status & 1)) break;
        unsigned code = in8(0x60);
        if (status & 0xe0) continue;
        if (code == 0xe0 || code == 0xe1) { extended = 1; continue; }
        if (extended) { extended = 0; continue; }
        if (code == 42) { shifts |= 1; continue; }
        if (code == 54) { shifts |= 2; continue; }
        if (code == 170) { shifts &= ~1u; continue; }
        if (code == 182) { shifts &= ~2u; continue; }
        if (code == 58) { caps ^= 1; continue; }
        if (code & 128) continue;
        char c = normal[code];
        if (c >= 'a' && c <= 'z') { if (!!shifts != !!caps) c -= 'a' - 'A'; }
        else if (shifts && shifted[code]) c = shifted[code];
        if (c) return c;
    }
    return -1;
}
