#ifndef PCI_H
#define PCI_H
#include <stdint.h>

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint8_t  pci_read8 (uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v);

/* Найти PCI-устройство по class/subclass/progif. Возвращает 0 при успехе. */
int pci_find(uint8_t cls, uint8_t subcls, uint8_t progif,
             uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func);

#endif
