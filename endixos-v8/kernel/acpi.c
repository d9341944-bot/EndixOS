/* Настоящее выключение через ACPI: RSDP -> RSDT -> FADT -> PM1_CNT, SLP_TYP берём из _S5_ в DSDT. */
#include "acpi.h"
#include "io.h"

static int sig4(const u8 *p, const char *s) { return p[0] == (u8)s[0] && p[1] == (u8)s[1] && p[2] == (u8)s[2] && p[3] == (u8)s[3]; }
static u32 rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static int csum_ok(const u8 *p, u32 n) { u8 s = 0; for (u32 i = 0; i < n; i++) s = (u8)(s + p[i]); return s == 0; }

int acpi_parse_s5(const u8 *d, u32 len, u8 *a, u8 *b)
{
    for (u32 i = 0; i + 12 < len; i++) {
        if (!sig4(d + i, "_S5_") || d[i + 4] != 0x12) continue;
        const u8 *p = d + i + 5;
        p += ((*p >> 6) & 3) + 1;          /* длина пакета */
        p++;                               /* число элементов */
        if (*p == 0x0A) p++;
        *a = *p++;
        if (*p == 0x0A) p++;
        *b = *p;
        return 0;
    }
    return -1;
}

static const u8 *find_rsdp(void)
{
    u32 ebda = (u32)(*(volatile u16 *)0x40E) << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000)
        for (u32 a = ebda; a < ebda + 1024; a += 16)
            if (sig4((u8 *)a, "RSD ") && sig4((u8 *)a + 4, "PTR ") && csum_ok((u8 *)a, 20)) return (u8 *)a;
    for (u32 a = 0xE0000; a < 0x100000; a += 16)
        if (sig4((u8 *)a, "RSD ") && sig4((u8 *)a + 4, "PTR ") && csum_ok((u8 *)a, 20)) return (u8 *)a;
    return 0;
}

int acpi_poweroff(void)
{
    const u8 *r = find_rsdp();
    if (!r) return -1;
    const u8 *rsdt = (const u8 *)rd32(r + 16);
    if (!rsdt || !sig4(rsdt, "RSDT")) return -2;
    u32 n = (rd32(rsdt + 4) - 36) / 4;
    const u8 *fadt = 0;
    for (u32 i = 0; i < n && i < 64; i++) {
        const u8 *t = (const u8 *)rd32(rsdt + 36 + i * 4);
        if (t && sig4(t, "FACP")) { fadt = t; break; }
    }
    if (!fadt) return -3;
    const u8 *dsdt = (const u8 *)rd32(fadt + 40);
    u32 smi = rd32(fadt + 48); u8 en = fadt[52];
    u16 pm1a = (u16)rd32(fadt + 64), pm1b = (u16)rd32(fadt + 68);
    if (!dsdt || !sig4(dsdt, "DSDT") || !pm1a) return -4;
    u8 ta, tb;
    if (acpi_parse_s5(dsdt, rd32(dsdt + 4), &ta, &tb)) return -5;
    if (!(inw(pm1a) & 1) && smi && en) {               /* включить режим ACPI, если BIOS ещё не включил */
        outb((u16)smi, en);
        for (u32 i = 0; i < 3000000 && !(inw(pm1a) & 1); i++) io_wait();
    }
    outw(pm1a, (u16)((ta << 10) | (1 << 13)));
    if (pm1b) outw(pm1b, (u16)((tb << 10) | (1 << 13)));
    for (u32 i = 0; i < 5000000; i++) io_wait();        /* ждём, пока питание уйдёт */
    return -6;
}
