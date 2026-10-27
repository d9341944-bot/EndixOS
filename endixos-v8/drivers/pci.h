#ifndef ENDIX_PCI_H
#define ENDIX_PCI_H
#include "../kernel/types.h"

u32  pci_read32(u32 bus, u32 dev, u32 fn, u32 off);
void pci_write32(u32 bus, u32 dev, u32 fn, u32 off, u32 v);
/* Ищет устройство vendor:device; 1 - найдено (bus/dev/fn заполнены). */
int  pci_find(u16 vendor, u16 device, u32 *bus, u32 *dev, u32 *fn);
/* Включает память и bus mastering. */
void pci_enable(u32 bus, u32 dev, u32 fn);
#endif
