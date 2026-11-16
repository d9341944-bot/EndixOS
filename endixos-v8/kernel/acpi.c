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

/* Диагностика: что нашли и на каком шаге остановились (для экрана выключения и команды `acpi`). */
static char diag[160];
static int dn;
static void dput(const char *t) { while (*t && dn < (int)sizeof diag - 1) diag[dn++] = *t++; diag[dn] = 0; }
static void dhex(u32 v) { static const char h[] = "0123456789ABCDEF"; char b[9]; int i = 0; for (int s = 28; s >= 0; s -= 4) { if (i || (v >> s) || s == 0) b[i++] = h[(v >> s) & 15]; } b[i] = 0; dput(b); }
const char *acpi_diag(void) { return diag; }

/* найти таблицу по подписи в RSDT (и в XSDT, если RSDT нет); nth - какую по счёту (для нескольких SSDT) */
static const u8 *find_table(const u8 *rsdt, const u8 *xsdt, const char *sg, int nth)
{
    int seen = 0;
    if (rsdt && sig4(rsdt, "RSDT")) {
        u32 n = (rd32(rsdt + 4) - 36) / 4;
        for (u32 i = 0; i < n && i < 64; i++) {
            const u8 *t = (const u8 *)rd32(rsdt + 36 + i * 4);
            if (t && sig4(t, sg) && seen++ == nth) return t;
        }
    }
    if (xsdt && sig4(xsdt, "XSDT")) {                  /* адреса 64-битные: берём младшие 32 бита (память до 4 ГБ) */
        u32 n = (rd32(xsdt + 4) - 36) / 8;
        for (u32 i = 0; i < n && i < 64; i++) {
            const u8 *t = (const u8 *)rd32(xsdt + 36 + i * 8);
            if (t && sig4(t, sg) && seen++ == nth) return t;
        }
    }
    return 0;
}

int acpi_poweroff(void)
{
    dn = 0; diag[0] = 0;
    const u8 *r = find_rsdp();
    if (!r) { dput("no RSDP"); return -1; }
    const u8 *rsdt = (const u8 *)rd32(r + 16);
    const u8 *xsdt = r[15] >= 2 ? (const u8 *)rd32(r + 24) : 0;
    if ((!rsdt || !sig4(rsdt, "RSDT")) && (!xsdt || !sig4(xsdt, "XSDT"))) { dput("no RSDT/XSDT"); return -2; }
    const u8 *fadt = find_table(rsdt, xsdt, "FACP", 0);
    if (!fadt) { dput("no FADT"); return -3; }
    const u8 *dsdt = (const u8 *)rd32(fadt + 40);
    u32 smi = rd32(fadt + 48); u8 en = fadt[52];
    u16 evt = (u16)rd32(fadt + 56);
    u16 pm1a = (u16)rd32(fadt + 64), pm1b = (u16)rd32(fadt + 68);
    if (!pm1a && rd32(fadt + 4) >= 244 && fadt[172] == 1) pm1a = (u16)rd32(fadt + 176);          /* X_PM1a_CNT_BLK */
    if (!pm1a) { dput("no PM1a_CNT"); return -4; }
    dput("PM1a="); dhex(pm1a); dput(" ");
    u8 ta = 0, tb = 0; int have = 0;
    if (dsdt && sig4(dsdt, "DSDT") && acpi_parse_s5(dsdt, rd32(dsdt + 4), &ta, &tb) == 0) have = 1;
    for (int k = 0; !have && k < 8; k++) {             /* _S5_ бывает и в SSDT */
        const u8 *t = find_table(rsdt, xsdt, "SSDT", k);
        if (!t) break;
        if (acpi_parse_s5(t, rd32(t + 4), &ta, &tb) == 0) have = 1;
    }
    dput(have ? "S5=" : "no _S5_, guessing "); if (have) { dhex(ta); dput(" "); }
    if (!(inw(pm1a) & 1) && smi && en) {               /* включить режим ACPI, если BIOS ещё не включил */
        outb((u16)smi, en);
        for (u32 i = 0; i < 3000000 && !(inw(pm1a) & 1); i++) io_wait();
    }
    dput((inw(pm1a) & 1) ? "SCI_EN " : "no SCI_EN ");
    if (evt) outw(evt, 0x8731);                        /* сбросить статусы (WAK_STS и др.), как делает Linux */
    /* порядок как в Linux: сначала SLP_TYP, затем SLP_EN. Типы-кандидаты: из _S5_, потом 7 и 5 (Intel ICH). */
    u8 cand[3] = { ta, 7, 5 }; u8 candb[3] = { tb, 7, 5 };
    for (int c = have ? 0 : 1; c < 3; c++) {
        if (c > 0 && have && cand[c] == ta) continue;
        __asm__ volatile("wbinvd");
        u16 v = inw(pm1a) & ~(0x1C00 | (1 << 13)); v = (u16)(v | (cand[c] << 10));
        outw(pm1a, v);
        if (pm1b) { u16 w = inw(pm1b) & ~(0x1C00 | (1 << 13)); outw(pm1b, (u16)(w | (candb[c] << 10))); }
        outw(pm1a, (u16)(v | (1 << 13)));
        if (pm1b) { u16 w = inw(pm1b) & ~(0x1C00 | (1 << 13)); outw(pm1b, (u16)(w | (candb[c] << 10) | (1 << 13))); }
        for (u32 i = 0; i < 3000000; i++) io_wait();   /* ждём, пока питание уйдёт */
    }
    dput("still on");
    return -6;
}
