#include "pci.h"
#include "io.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

static uint32_t make_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    return (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) | (off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    outl(PCI_ADDR, make_addr(bus, slot, func, off));
    return inl(PCI_DATA);
}
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_read32(bus, slot, func, off);
    return (v >> ((off & 2) * 8)) & 0xFFFF;
}
uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_read32(bus, slot, func, off);
    return (v >> ((off & 3) * 8)) & 0xFF;
}
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v) {
    outl(PCI_ADDR, make_addr(bus, slot, func, off));
    outl(PCI_DATA, v);
}

int pci_find(uint8_t cls, uint8_t subcls, uint8_t progif,
             uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t id = pci_read32(bus, slot, func, 0);
                if (id == 0xFFFFFFFF) continue;
                uint8_t c  = pci_read8(bus, slot, func, 0x0B);
                uint8_t sc = pci_read8(bus, slot, func, 0x0A);
                uint8_t pi = pci_read8(bus, slot, func, 0x09);
                if (c == cls && sc == subcls && pi == progif) {
                    *out_bus = bus; *out_slot = slot; *out_func = func;
                    return 0;
                }
            }
        }
    }
    return -1;
}
