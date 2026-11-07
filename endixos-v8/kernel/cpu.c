/* Определение процессора через CPUID. */
#include "cpu.h"
#include "types.h"

static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0));
}

void cpu_brand(char *out)
{
    u32 a, b, c, d;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a < 0x80000004) {                  /* строки бренда нет */
        const char *fallback = "Unknown x86 CPU";
        int i = 0;
        while (fallback[i]) { out[i] = fallback[i]; i++; }
        out[i] = 0;
        return;
    }

    u32 raw[12];
    for (u32 i = 0; i < 3; i++)
        cpuid(0x80000002 + i, &raw[i * 4], &raw[i * 4 + 1], &raw[i * 4 + 2], &raw[i * 4 + 3]);

    const char *src = (const char *)raw;
    int start = 0, n = 0;
    while (start < 48 && src[start] == ' ')
        start++;                           /* убрать ведущие пробелы */
    for (int i = start; i < 48 && src[i]; i++)
        out[n++] = src[i];
    out[n] = 0;
}
