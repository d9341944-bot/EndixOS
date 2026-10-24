#ifndef ENDIX_IO_H
#define ENDIX_IO_H

#include "types.h"

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

/* Запретить прерывания и вернуть прежние EFLAGS. */
static inline u32 irq_save(void)
{
    u32 flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(u32 flags)
{
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

#endif
