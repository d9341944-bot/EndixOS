/* Собственная GDT: плоская модель, сегменты кода (0x08) и данных (0x10). */
#include "gdt.h"
#include "types.h"

typedef struct {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  gran;
    u8  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt[3];
static gdt_ptr_t   gdt_ptr;

static void gdt_set(int i, u32 base, u32 limit, u8 access, u8 gran)
{
    gdt[i].base_low   = (u16)(base & 0xFFFF);
    gdt[i].base_mid   = (u8)((base >> 16) & 0xFF);
    gdt[i].base_high  = (u8)((base >> 24) & 0xFF);
    gdt[i].limit_low  = (u16)(limit & 0xFFFF);
    gdt[i].gran       = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access     = access;
}

void gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0);                  /* нулевой дескриптор */
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC0);      /* код: ring0, 4 КБ гранулярность, 32 бита */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);      /* данные */

    gdt_ptr.limit = (u16)(sizeof(gdt) - 1);
    gdt_ptr.base  = (u32)&gdt;

    __asm__ volatile(
        "lgdt %0\n\t"
        "ljmp $0x08, $1f\n"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        : : "m"(gdt_ptr) : "eax", "memory");
}
