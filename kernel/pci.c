#include "pci.h"
#include "io.h"
#include "vga.h"
#define PCI_MAX 64
static struct pci_device devices[PCI_MAX];
static unsigned count, omitted;
static uint32_t address(unsigned bus, unsigned slot, unsigned function, unsigned offset) {
    return 0x80000000u | bus << 16 | slot << 11 | function << 8 | (offset & 0xFC);
}
static uint32_t read(unsigned b, unsigned s, unsigned f, unsigned offset) {
    outl(0xCF8, address(b, s, f, offset));
    return inl(0xCFC);
}
void pci_init(void) {
    count = omitted = 0;
    for (unsigned b = 0; b < 256; b++)
        for (unsigned s = 0; s < 32; s++) {
            if ((read(b, s, 0, 0) & 0xFFFF) == 0xFFFF)
                continue;
            unsigned functions = (read(b, s, 0, 12) & 0x00800000) ? 8 : 1;
            for (unsigned f = 0; f < functions; f++) {
                uint32_t id = read(b, s, f, 0);
                if ((id & 0xFFFF) == 0xFFFF)
                    continue;
                if (count == PCI_MAX) {
                    omitted++;
                    continue;
                }
                struct pci_device *d = &devices[count++];
                d->bus = b;
                d->slot = s;
                d->function = f;
                d->vendor = id;
                d->device = id >> 16;
                uint32_t classes = read(b, s, f, 8);
                d->class_code = classes >> 24;
                d->subclass = classes >> 16;
                d->bar0 = read(b, s, f, 16);
                d->irq = read(b, s, f, 60);
            }
        }
}
const struct pci_device *pci_find(uint16_t vendor, uint16_t device) {
    for (unsigned i = 0; i < count; i++)
        if (devices[i].vendor == vendor && devices[i].device == device)
            return &devices[i];
    return 0;
}
void pci_enable_io_dma(const struct pci_device *d) {
    uint32_t command = read(d->bus, d->slot, d->function, 4);
    outl(0xCF8, address(d->bus, d->slot, d->function, 4));
    outw(0xCFC, (uint16_t)command | 5); /* I/O decode and bus mastering, no status W1C */
}
void pci_list(void) {
    vga_puts("PCI devices=");
    vga_put_dec(count);
    vga_puts(" omitted=");
    vga_put_dec(omitted);
    vga_putc('\n');
    for (unsigned i = 0; i < count; i++) {
        const struct pci_device *d = &devices[i];
        vga_put_dec(d->bus);
        vga_putc(':');
        vga_put_dec(d->slot);
        vga_putc('.');
        vga_put_dec(d->function);
        vga_puts(" id=");
        vga_put_hex(((uint32_t)d->vendor << 16) | d->device);
        vga_puts(" class=");
        vga_put_hex(((uint32_t)d->class_code << 8) | d->subclass);
        vga_puts(" BAR0=");
        vga_put_hex(d->bar0);
        vga_putc('\n');
    }
}
