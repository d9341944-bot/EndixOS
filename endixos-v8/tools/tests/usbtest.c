/* Хост-тест драйвера USB: поддельный контроллер EHCI (в usb_idle разбирает очередь qTD) и поддельная флешка (Bulk-Only + SCSI).
 * Проверяет логику драйвера по спецификации, на реальном железе не проверялось. */
#define ENDIX_HOST_LIBC
#define USB_TEST
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../drivers/usb.c"
#include "../../kernel/fat32.c"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } else printf("PASS: %s\n", m); } while (0)

static u8 mmio[256] __attribute__((aligned(256)));
static u8 *rdisk; static const u32 RSEC = 70000;
u32 pit_ticks(void) { static u32 t; return t++; }
void pci_enable(u32 b, u32 d, u32 f) { (void)b; (void)d; (void)f; }
void pci_write32(u32 b, u32 d, u32 f, u32 o, u32 v) { (void)b; (void)d; (void)f; (void)o; (void)v; }
u32 pci_read32(u32 b, u32 d, u32 f, u32 o)
{
    if (b || d != 3 || f) return 0xFFFFFFFFu;
    switch (o) { case 0: return 0x293A8086; case 8: return 0x0C032000; case 0xC: return 0; case 0x10: return PA(mmio); }
    return 0;
}
#define M32(o) (*(volatile u32 *)(mmio + (o)))
#define OP 0x20

static int dev_addr, cfg_set, exp_tog[16], tur_fail = 2, bad_fifo;
static u8 setup[8], resp[512]; static int resp_len, pend_addr = -1;
static int bs; /* 0 ждём CBW, 1 данные IN, 2 данные OUT, 3 CSW */
static u8 bot_data[32768]; static u32 bot_len, bot_got, bot_tag, bot_lba, bot_cnt; static int bot_stat;

static const u8 DEVD[18] = { 18, 1, 0, 2, 0, 0, 0, 64, 0x51, 0x09, 0x66, 0x16, 0, 1, 1, 2, 3, 1 };
static const u8 CFGD[32] = { 9, 2, 32, 0, 1, 1, 0, 0x80, 50, 9, 4, 0, 0, 2, 8, 6, 0x50, 0, 7, 5, 0x81, 2, 0, 2, 0, 7, 5, 0x02, 2, 0, 2, 0 };

static int pkts(u32 len, u32 mps) { return len ? (int)((len + mps - 1) / mps) : 1; }

static int exec_q(int ep, qtd_t *q, u32 *rem)
{
    u32 tk = q->tok; int pid = (tk >> 8) & 3, dt = (int)(tk >> 31); u32 len = (tk >> 16) & 0x7FFF;
    u8 *p = (u8 *)(unsigned long)q->buf[0];
    for (int i = 1; i < 5; i++) if (q->buf[i] != ((q->buf[0] & ~0xFFFu) + 4096u * i)) { printf("  bad page ptr\n"); return -1; }
    *rem = 0;
    if (ep == 0) {
        if (pid == 2) { if (dt || len != 8) return -1; memcpy(setup, p, 8); resp_len = 0; pend_addr = -1;
            u16 val = (u16)(setup[2] | setup[3] << 8);
            if (setup[1] == 6 && (val >> 8) == 1) { memcpy(resp, DEVD, 18); resp_len = 18; }
            else if (setup[1] == 6 && (val >> 8) == 2) { memcpy(resp, CFGD, 32); resp_len = 32; }
            else if (setup[1] == 5) pend_addr = val;
            else if (setup[1] == 9) { if (val != 1) return -1; cfg_set = 1; }
            else if (setup[1] == 1) { exp_tog[setup[4] & 15] = 0; }
            return 0; }
        if (pid == 1 && (setup[0] & 0x80) && len && !dt) return -1;
        if (len) { if (pid == 1) { u32 n = len < (u32)resp_len ? len : (u32)resp_len; memcpy(p, resp, n); *rem = len - n; } return 0; }
        if (!dt) return -1;
        if (pend_addr >= 0) dev_addr = pend_addr;
        return 0;
    }
    int in = pid == 1; u32 n = len;
    if (ep != (in ? 1 : 2)) return -1;
    if (exp_tog[ep] != dt) { printf("  bad toggle on ep %d\n", ep); bad_fifo++; return -1; }
    if (!in && bs == 0) {                                   /* CBW */
        if (len != 31 || memcmp(p, "USBC", 4)) return -1;
        bot_tag = (u32)p[4] | (u32)p[5] << 8 | (u32)p[6] << 16 | (u32)p[7] << 24;
        bot_len = (u32)p[8] | (u32)p[9] << 8 | (u32)p[10] << 16 | (u32)p[11] << 24;
        const u8 *c = p + 15; bot_stat = 0; bot_got = 0;
        int dir_in = p[12] & 0x80;
        memset(bot_data, 0, sizeof bot_data);
        if (c[0] == 0x12) { memcpy(bot_data + 8, "EndixTst", 8); memcpy(bot_data + 16, "FakeFlash 2GB   ", 16); }
        else if (c[0] == 0x00) { if (tur_fail > 0) { tur_fail--; bot_stat = 1; } }
        else if (c[0] == 0x25) { u32 l = RSEC - 1; bot_data[0] = (u8)(l >> 24); bot_data[1] = (u8)(l >> 16); bot_data[2] = (u8)(l >> 8); bot_data[3] = (u8)l; bot_data[6] = 2; }
        else if (c[0] == 0x28 || c[0] == 0x2A) {
            bot_lba = (u32)c[2] << 24 | (u32)c[3] << 16 | (u32)c[4] << 8 | c[5]; bot_cnt = (u32)c[7] << 8 | c[8];
            if (bot_len != bot_cnt * 512 || bot_lba + bot_cnt > RSEC) return -1;
            if (c[0] == 0x28) memcpy(bot_data, rdisk + bot_lba * 512, bot_len);
        } else if (c[0] == 0x03) { bot_data[0] = 0x70; }
        else return -1;
        bs = bot_len ? (dir_in ? 1 : 2) : 3;
    } else if (!in && bs == 2) {
        memcpy(bot_data + bot_got, p, len); bot_got += len;
        if (bot_got >= bot_len) { memcpy(rdisk + bot_lba * 512, bot_data, bot_len); bs = 3; }
    } else if (in && bs == 1) {
        n = len; if (n > bot_len - bot_got) n = bot_len - bot_got;
        memcpy(p, bot_data + bot_got, n); bot_got += n; *rem = len - n;
        if (bot_got >= bot_len) bs = 3;
    } else if (in && bs == 3) {
        if (len != 13) return -1;
        memcpy(p, "USBS", 4); p[4] = (u8)bot_tag; p[5] = p[6] = p[7] = 0; memset(p + 8, 0, 4); p[12] = (u8)bot_stat; bs = 0;
    } else { printf("  BOT phase error (state %d, in %d)\n", bs, in); return -1; }
    exp_tog[ep] ^= pkts(n, 512) & 1;
    return 0;
}

static int prev_pr;
void usb_idle(void)
{
    u32 cmd = M32(OP);
    if (cmd & 2) { M32(OP) = cmd &= ~2u; }
    if (cmd & 1) M32(OP + 4) &= ~(1u << 12); else M32(OP + 4) |= 1u << 12;
    if ((cmd & 32) && (cmd & 1)) M32(OP + 4) |= 1u << 15; else M32(OP + 4) &= ~(1u << 15);
    u32 ps = M32(OP + 0x44);
    if (!(ps & 0x1000) && !(ps & 1) && !(ps & 0x2000)) ps = 1 | 0x1000;
    if (ps & 0x100) prev_pr = 1; else if (prev_pr) { prev_pr = 0; ps |= 4; }
    M32(OP + 0x44) = ps;
    if (!(cmd & 32) || !(cmd & 1)) return;
    qh_t *h = (qh_t *)(unsigned long)M32(OP + 0x18), *q = (qh_t *)(unsigned long)(h->link & ~31u);
    if (q == h || (q->nxt & 1)) return;
    int addr = q->ch & 0x7F, ep = (q->ch >> 8) & 15;
    if (!(q->ch & (1u << 14)) || ((q->ch >> 12) & 3) != 2 || addr != dev_addr) { return; }
    u32 nxt = q->nxt;
    while (!(nxt & 1)) {
        qtd_t *t = (qtd_t *)(unsigned long)(nxt & ~31u);
        if (!(t->tok & 0x80)) break;
        u32 rem; int r = exec_q(ep, t, &rem);
        t->tok &= ~0x80u; t->tok = (t->tok & ~(0x7FFFu << 16)) | rem << 16;
        if (r) { t->tok |= 0x40; break; }
        nxt = (rem && ((t->tok >> 8) & 3) == 1) ? t->alt : t->nxt;
    }
    q->nxt = 1;
}

static char lbuf[300];
static int lst(void *c, const char *n, const char *a, u32 s, int d) { (void)c; (void)a; (void)d; strcat(lbuf, n); strcat(lbuf, ":"); char t[12]; sprintf(t, "%u ", s); strcat(lbuf, t); return 0; }

int main(void)
{
    rdisk = calloc(RSEC, 512);
    M32(0) = 0x20 | 0x01000000; M32(4) = 2; M32(8) = 0;                 /* CAPLENGTH, HCSPARAMS: 2 порта */
    int n = usb_scan();
    printf("%s", usb_log());
    CHECK(n == 1, "найден 1 накопитель");
    CHECK(!strncmp(usb_drive_name(0), "EndixTst FakeFlash 2GB", 22), "имя из INQUIRY");
    blk_t *b = usb_blk(0);
    CHECK(b && b->sectors == RSEC, "ёмкость из READ CAPACITY");
    CHECK(dev_addr == 1 && cfg_set, "адрес 1 и конфигурация заданы");
    if (!b) return 1;
    static u8 w[32768], r[32768];
    for (int i = 0; i < 32768; i++) w[i] = (u8)(i * 7 + i / 512);
    CHECK(b->wr(b->ctx, 100, 64, w) == 0, "запись 64 секторов (2 qTD)");
    CHECK(!memcmp(rdisk + 100 * 512, w, 32768), "данные легли на диск");
    CHECK(b->rd(b->ctx, 100, 64, r) == 0 && !memcmp(r, w, 32768), "чтение 64 секторов");
    CHECK(b->rd(b->ctx, 5, 1, r) == 0 && b->wr(b->ctx, 5, 1, w) == 0 && b->rd(b->ctx, 5, 1, r) == 0 && !memcmp(r, w, 512), "один сектор туда-обратно");
    CHECK(b->rd(b->ctx, RSEC - 1, 2, r) != 0, "чтение за концом диска даёт ошибку");
    CHECK(b->rd(b->ctx, 0, 3, r) == 0, "после ошибки всё работает (восстановление)");
    CHECK(bad_fifo == 0, "переключения DATA0/DATA1 верны");
    fat_t f;
    CHECK(fat_format(b, 0, RSEC, "USBTEST", &f) == 0, "fat_format на флешке");
    const char *msg = "hello from usb stick";
    CHECK(fat_write(&f, "/HELLO.TXT", msg, (u32)strlen(msg)) == 0 && fat_sync(&f) == 0, "fat_write");
    fat_t g; char buf[64] = { 0 };
    CHECK(fat_mount(b, 0, &g) == 0 && fat_read(&g, "/HELLO.TXT", buf, 63) == (int)strlen(msg) && !strcmp(buf, msg), "fat_mount + fat_read");
    fat_list(&g, "/", lst, 0); printf("ls: %s\n", lbuf);
    CHECK(strstr(lbuf, "HELLO.TXT:20"), "fat_list видит файл");
    printf("fails=%d\n", fails);
    return fails != 0;
}
