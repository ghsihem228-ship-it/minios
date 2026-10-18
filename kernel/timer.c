/* timer.c - Programmable Interval Timer (PIT), channel 0 on IRQ0
 *
 * The PIT's input clock runs at 1,193,182 Hz. We program a divisor so it
 * fires `hz` times per second, count the ticks, and refresh the uptime shown
 * in the status bar once per second.
 */

#include "timer.h"
#include "irq.h"
#include "pic.h"
#include "io.h"
#include "vga.h"

#define PIT_BASE_HZ 1193182
#define PIT_CMD     0x43
#define PIT_CH0     0x40

static volatile uint32_t ticks = 0;
static uint32_t timer_hz = 100;

static void format_uptime(uint32_t seconds, char *out) {
    const char *prefix = "Uptime: ";
    int n = 0;

    while (*prefix)
        out[n++] = *prefix++;

    char digits[10];
    int d = 0;
    do {
        digits[d++] = (char)('0' + seconds % 10);
        seconds /= 10;
    } while (seconds > 0 && d < 10);

    while (d > 0)
        out[n++] = digits[--d];

    out[n++] = 's';
    out[n] = '\0';
}

static void timer_callback(struct regs *r) {
    (void)r;
    ticks++;

    if (ticks % timer_hz == 0) {
        char text[32];
        format_uptime(ticks / timer_hz, text);
        vga_set_status_right(text);
    }
}

uint32_t timer_ticks(void) {
    return ticks;
}

void timer_init(uint32_t hz) {
    timer_hz = hz;
    uint32_t divisor = PIT_BASE_HZ / hz;

    outb(PIT_CMD, 0x36);                          /* channel 0, square wave, 16-bit */
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));

    vga_set_status_right("Uptime: 0s");
    irq_register_handler(0, timer_callback);
    pic_unmask(0);
}
