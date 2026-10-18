/* keyboard.c - PS/2 keyboard driver (IRQ1, scancode set 1, US layout)
 *
 * The keyboard controller sends a scancode on port 0x60 for every key press
 * and release. The IRQ handler translates press codes to ASCII and stores
 * them in a ring buffer; keyboard_getchar() hands them to the rest of the
 * kernel (soon: the shell).
 */

#include <stdint.h>
#include "keyboard.h"
#include "irq.h"
#include "pic.h"
#include "io.h"

#define KBD_DATA_PORT 0x60
#define BUFFER_SIZE   256

static const char keymap[58] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char keymap_shift[58] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static volatile char buffer[BUFFER_SIZE];
static volatile uint32_t head = 0;   /* written by the IRQ handler */
static volatile uint32_t tail = 0;   /* read by keyboard_getchar */

static int shift_down = 0;
static int extended = 0;

static void buffer_push(char c) {
    uint32_t next = (head + 1) % BUFFER_SIZE;
    if (next == tail)
        return;                      /* buffer full: drop the key */
    buffer[head] = c;
    head = next;
}

static void keyboard_callback(struct regs *r) {
    (void)r;
    uint8_t sc = inb(KBD_DATA_PORT);

    if (sc == 0xE0) {                /* prefix for arrows, Home, Delete... */
        extended = 1;
        return;
    }
    if (extended) {                  /* ignore the extended key for now */
        extended = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) {  /* left or right shift pressed */
        shift_down = 1;
        return;
    }
    if (sc == 0xAA || sc == 0xB6) {  /* left or right shift released */
        shift_down = 0;
        return;
    }

    if (sc & 0x80)                   /* any other key release */
        return;

    if (sc < sizeof(keymap)) {
        char c = shift_down ? keymap_shift[sc] : keymap[sc];
        if (c)
            buffer_push(c);
    }
}

void keyboard_init(void) {
    irq_register_handler(1, keyboard_callback);
    pic_unmask(1);
}

char keyboard_getchar(void) {
    for (;;) {
        __asm__ volatile ("cli");
        if (head != tail) {
            char c = buffer[tail];
            tail = (tail + 1) % BUFFER_SIZE;
            __asm__ volatile ("sti");
            return c;
        }
        /* "sti; hlt" is atomic enough: the CPU delays interrupts until after
         * the instruction following sti, so a key cannot slip in between. */
        __asm__ volatile ("sti; hlt");
    }
}
