#ifndef PCI_H
#define PCI_H
#include <stdint.h>
struct pci_device {
    uint8_t bus, slot, function, irq, class_code, subclass;
    uint16_t vendor, device;
    uint32_t bar0;
};
void pci_init(void);
const struct pci_device *pci_find(uint16_t vendor, uint16_t device);
void pci_enable_io_dma(const struct pci_device *dev);
void pci_list(void);
#endif
