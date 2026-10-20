/* RTL8139 I/O BAR, coherent x86 DMA, polling with NIC IRQs disabled. Static
 * DMA buffers lie in the kernel's PMM-reserved, low identity-mapped image. */
#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#define RX_SIZE 8192u
static volatile uint8_t rx[RX_SIZE + 16 + 1536] __attribute__((aligned(256)));
static uint8_t tx[4][1536] __attribute__((aligned(16)));
static uint16_t base;
static unsigned rx_pos, tx_slot;
static uint32_t errors;

int rtl_init(uint8_t mac[6]) {
    const struct pci_device *d = pci_find(0x10EC, 0x8139);
    base = 0;
    if (!d)
        return -19;
    if (!(d->bar0 & 1) || (d->bar0 & ~3u) > 0xFF00u)
        return -95;
    base = d->bar0 & ~3u;
    pci_enable_io_dma(d);
    outb(base + 0x52, 0);
    outb(base + 0x37, 0x10);
    unsigned n;
    for (n = 0; n < 100000; n++)
        if (!(inb(base + 0x37) & 0x10))
            break;
    if (n == 100000) {
        base = 0;
        return -110;
    }
    for (n = 0; n < 6; n++)
        mac[n] = inb(base + n);
    rx_pos = tx_slot = errors = 0;
    outw(base + 0x3C, 0); /* IMR, no IRQ-side parsing */
    outw(base + 0x3E, 0xFFFF);
    outl(base + 0x30, (uint32_t)rx);
    for (n = 0; n < 4; n++)
        outl(base + 0x20 + 4 * n, (uint32_t)tx[n]);
    outb(base + 0x37, 0x0C);       /* enable receiver and transmitter */
    outl(base + 0x44, 0x0000E70A); /* 8 KiB ring; wrap, physical+broadcast */
    outl(base + 0x40, 0x03000700); /* standard inter-frame gap, DMA burst */
    outw(base + 0x38, 0xFFF0);
    return 0;
}
int rtl_link(void) { return base && !(inb(base + 0x58) & 4); }
int rtl_send(const uint8_t *frame, unsigned length) {
    if (!base || !rtl_link())
        return -100;
    if (length < 14 || length > ETH_FRAME_MAX)
        return -22;
    uint32_t state = inl(base + 0x10 + 4 * tx_slot);
    if (!(state & 0x2000))
        return -11;
    if (state & 0x40004000u)
        errors++;
    unsigned sent = length < 60 ? 60 : length;
    for (unsigned i = 0; i < sent; i++)
        tx[tx_slot][i] = i < length ? frame[i] : 0;
    __asm__ volatile("" ::: "memory");
    outl(base + 0x10 + 4 * tx_slot, sent);
    tx_slot = (tx_slot + 1) % 4;
    return 0;
}
int rtl_receive(uint8_t *frame, unsigned capacity) {
    if (!base || (inb(base + 0x37) & 1))
        return 0;
    __asm__ volatile("" ::: "memory");
    uint16_t status = rx[rx_pos] | ((uint16_t)rx[rx_pos + 1] << 8);
    uint16_t size = rx[rx_pos + 2] | ((uint16_t)rx[rx_pos + 3] << 8);
    if (size < 4 || size > 1522 || !(status & 1)) {
        errors++;
        /* Drop the corrupted ring by restarting RX at the beginning. */
        outb(base + 0x37, 4);
        rx_pos = 0;
        outl(base + 0x30, (uint32_t)rx);
        outb(base + 0x37, 0x0C);
        outw(base + 0x38, 0xFFF0);
        outw(base + 0x3E, 0xFFFF);
        return -5;
    }
    unsigned length = size - 4;
    if (length > capacity) {
        errors++;
        length = 0;
    }
    for (unsigned i = 0; i < length; i++)
        frame[i] = rx[(rx_pos + 4 + i) % RX_SIZE];
    rx_pos = (rx_pos + size + 4 + 3) & ~3u;
    rx_pos %= RX_SIZE;
    outw(base + 0x38, (uint16_t)(rx_pos - 16));
    outw(base + 0x3E, 0xFFFF);
    return length ? (int)length : -5;
}
uint32_t rtl_errors(void) { return errors; }
