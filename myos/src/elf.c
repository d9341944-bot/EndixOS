#include "elf.h"
#include "tty.h"
#include <stdint.h>

static void memcpy8(uint8_t* dst, const uint8_t* src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}
static void memset8(uint8_t* dst, uint8_t v, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = v;
}

uint32_t elf_load(const void* image, uint32_t size) {
    const uint8_t* base = (const uint8_t*)image;
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)base;

    if (size < sizeof(Elf32_Ehdr))              { tty_puts("[elf] too small\n");  return 0; }
    if (eh->e_ident[0] != ELF_MAGIC_0 ||
        eh->e_ident[1] != ELF_MAGIC_1 ||
        eh->e_ident[2] != ELF_MAGIC_2 ||
        eh->e_ident[3] != ELF_MAGIC_3)          { tty_puts("[elf] bad magic\n");  return 0; }
    if (eh->e_ident[4] != 1)                    { tty_puts("[elf] not ELF32\n");  return 0; }
    if (eh->e_machine != 3)                     { tty_puts("[elf] not i386\n");   return 0; }

    tty_puts("[elf] phnum = "); tty_put_hex(eh->e_phnum);
    tty_puts(", entry = ");     tty_put_hex(eh->e_entry);
    tty_putc('\n');

    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr* ph = (const Elf32_Phdr*)(base + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        tty_puts("[elf] LOAD vaddr=");  tty_put_hex(ph->p_vaddr);
        tty_puts(" filesz=");           tty_put_hex(ph->p_filesz);
        tty_puts(" memsz=");            tty_put_hex(ph->p_memsz);
        tty_putc('\n');

        uint8_t* dst = (uint8_t*)ph->p_vaddr;
        memcpy8(dst, base + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            memset8(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
    }

    return eh->e_entry;
}
