#include "pci.h"
#include "../kernel/io.h"

static u32 addr(u32 b, u32 d, u32 f, u32 off)
{
    return 0x80000000u | b << 16 | d << 11 | f << 8 | (off & 0xFC);
}

u32 pci_read32(u32 b, u32 d, u32 f, u32 off)
{
    outl(0xCF8, addr(b, d, f, off));
    return inl(0xCFC);
}

void pci_write32(u32 b, u32 d, u32 f, u32 off, u32 v)
{
    outl(0xCF8, addr(b, d, f, off));
    outl(0xCFC, v);
}

int pci_find(u16 vendor, u16 device, u32 *bus, u32 *dev, u32 *fn)
{
    for (u32 b = 0; b < 8; b++)
        for (u32 d = 0; d < 32; d++)
            for (u32 f = 0; f < 8; f++) {
                u32 id = pci_read32(b, d, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (f == 0) break; continue; }
                if ((id & 0xFFFF) == vendor && (id >> 16) == device) { *bus = b; *dev = d; *fn = f; return 1; }
                if (f == 0 && !(pci_read32(b, d, 0, 0x0C) >> 16 & 0x80)) break;   /* не многофункциональное */
            }
    return 0;
}

void pci_enable(u32 b, u32 d, u32 f)
{
    u32 c = pci_read32(b, d, f, 4);
    pci_write32(b, d, f, 4, (c & 0xFFFF) | 0x6);       /* memory space + bus master */
}
