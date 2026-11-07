#define UPDATE_HOST 1
#include "gui/resolution.c"
#include "gui/update.c"
extern int printf(const char *, ...);
extern char *strstr(const char *, const char *);
static u8 disk[200000 * 512];
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
int ahci_init(void) { return 1; }
const ahci_disk_t *ahci_disk(int i) { static ahci_disk_t d = { 0, 200000, "t" }; return i == 0 ? &d : 0; }
int ahci_read(int d, u32 lba, u32 n, void *b) { (void)d; for (u32 i = 0; i < n * 512; i++) ((u8 *)b)[i] = disk[(u64)lba * 512 + i]; return 0; }
int ahci_write(int d, u32 lba, u32 n, const void *b) { (void)d; for (u32 i = 0; i < n * 512; i++) disk[(u64)lba * 512 + i] = ((const u8 *)b)[i]; return 0; }
u32 fb_phys_width(void) { return 1024; } u32 fb_width(void) { return 1024; } u32 fb_height(void) { return 768; }
static int rebooted; void sys_reboot(void) { rebooted = 1; }
static char last_note[80]; void wm_notify(const char *t, const char *x) { (void)t; int i = 0; while (x[i] && i < 79) { last_note[i] = x[i]; i++; } last_note[i] = 0; }

/* поддельная сеть */
static u8 newk[300000]; static char manifest[600];
static int srv_down, srv_corrupt, srv_http = 200, srv_trunc;
static net_fetch_t fi; static const u8 *pend; static u32 pend_n; static u8 *dst; static u32 dst_cap, delivered;
int net_fetch(const char *url, u8 *buf, u32 cap)
{
    if (srv_down) { fi.state = NF_ERROR; fi.err[0] = 'N'; fi.err[1] = 0; return -1; }
    dst = buf; dst_cap = cap; delivered = 0;
    if (strstr(url, "latest.txt")) { pend = (const u8 *)manifest; for (pend_n = 0; manifest[pend_n]; pend_n++) { } }
    else { pend = newk; pend_n = sizeof newk; }
    fi.state = NF_LOADING; fi.status = 0; fi.len = 0; fi.truncated = 0; return 0;
}
void net_fetch_info(net_fetch_t *f)
{
    if (fi.state == NF_LOADING) {                     /* отдаём порциями, как сеть */
        u32 step = pend_n / 5 + 1; delivered += step; if (delivered >= pend_n) {
            u32 n = pend_n < dst_cap ? pend_n : dst_cap - 1;
            for (u32 i = 0; i < n; i++) dst[i] = pend[i] ^ (srv_corrupt && i == 1000 ? 0xFF : 0);
            fi.len = n; fi.state = NF_DONE; fi.status = srv_http; fi.truncated = srv_trunc;
        } else fi.len = delivered;
    }
    *f = fi;
}
void net_fetch_abort(void) { fi.state = NF_IDLE; }
void net_info(net_info_t *i) { i->up = 1; }
static void hex(const u8 *h, char *o) { static const char x[] = "0123456789abcdef"; for (int i = 0; i < 32; i++) { o[2 * i] = x[h[i] >> 4]; o[2 * i + 1] = x[h[i] & 15]; } o[64] = 0; }
static void make_manifest(int build)
{
    u8 h[32]; char hx[65]; sha256(newk, sizeof newk, h); hex(h, hx);
    char *p = manifest; const char *a = "build="; while (*a) *p++ = *a++;
    *p++ = (char)('0' + build); *p++ = '\n';
    a = "version=8.1\nsize=300000\nnotes=New wallpapers|Faster Files\nsha256="; while (*a) *p++ = *a++;
    for (int i = 0; i < 64; i++) *p++ = hx[i];
    *p++ = '\n'; *p = 0;
}
static void run(u32 *now, int until_not_busy)
{
    for (int i = 0; i < 400; i++) { *now += 10; update_tick(*now); int s = update_state(); if (until_not_busy && s != UP_CHECKING && s != UP_DOWNLOADING && s != UP_INSTALLING) break; }
}
int main(void)
{
    static blk_t b; static fat_t f; static char out[4096], kern[300000];
    b.rd = d_rd; b.wr = d_wr; b.ctx = 0; b.sectors = 200000;
    for (u32 i = 0; i < sizeof newk; i++) newk[i] = (u8)(i * 7 + 3);
    u32 now = 3000;

    make_manifest(2);
    update_check(); run(&now, 1);
    CHECK(update_state() == UP_AVAILABLE && update_build() == 2 && update_size() == 300000, "найдено обновление build 2");
    CHECK(update_version()[0] == '8' && update_version()[2] == '1', "версия 8.1");
    CHECK(strstr(update_notes(), "Faster Files") != 0, "заметки прочитаны");
    CHECK(update_checked_secs(now) >= 0 && update_checked_secs(now) < 60, "время проверки");

    update_install();
    CHECK(update_state() == UP_ERROR, "без установки на диск обновляться нельзя");
    update_check(); run(&now, 1);

    res_set_installed(1);
    CHECK(mbr_set_single(&b, 2048, 190000, 0x0C) == 0 && fat_format(&b, 2048, 190000, "T", &f) == 0, "диск готов");
    fat_mkdir(&f, "/boot"); fat_mkdir(&f, "/boot/grub");
    for (u32 i = 0; i < sizeof kern; i++) kern[i] = (char)(i * 5 + 1);
    fat_write(&f, "/boot/endixos.elf", kern, sizeof kern);
    const char *cfg0 = "set default=0\nmenuentry \"EndixOS\" {\n    multiboot /boot/endixos.elf installed\n    module /boot/endixos.elf endixos.elf\n    boot\n}\n";
    { u32 n = 0; while (cfg0[n]) n++; fat_write(&f, "/boot/grub/grub.cfg", cfg0, n); }

    update_check(); run(&now, 1);
    update_install();
    int sawdl = 0, saw = 0; int prev = 0;
    for (int i = 0; i < 400 && update_state() != UP_READY && update_state() != UP_ERROR; i++) {
        now += 10; update_tick(now);
        if (update_state() == UP_DOWNLOADING) { sawdl = 1; if (update_percent() >= prev) saw++; prev = update_percent(); }
    }
    CHECK(update_state() == UP_READY && sawdl && saw > 1, "скачано и установлено, прогресс шёл");
    CHECK(update_percent() == 100, "прогресс 100%");
    int n = fat_read(&f, "/boot/endixos.elf", kern, sizeof kern);
    int same = n == (int)sizeof newk; for (int i = 0; same && i < n; i++) if ((u8)kern[i] != newk[i]) same = 0;
    CHECK(same, "новое ядро записано без искажений");
    n = fat_read(&f, "/boot/previous.elf", kern, sizeof kern);
    int old = n == 300000; for (int i = 0; old && i < n; i++) if ((u8)kern[i] != (u8)(i * 5 + 1)) old = 0;
    CHECK(old, "старая версия сохранена как previous.elf");
    n = fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out); out[n > 0 ? n : 0] = 0;
    CHECK(strstr(out, "previous version") && strstr(out, "/boot/previous.elf") && strstr(out, "/boot/endixos.elf"), "в меню GRUB есть пункт со старой версией");
    CHECK(last_note[0] != 0, "уведомление показано");

    const char *m; res_apply(3, &m);
    n = fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out); out[n > 0 ? n : 0] = 0;
    CHECK(strstr(out, "previous.elf"), "смена разрешения не теряет пункт старой версии");

    update_restart(); CHECK(rebooted, "перезагрузка");

    /* повреждённое скачивание */
    update_init(); srv_corrupt = 1; make_manifest(3); update_check(); run(&now, 1); update_install(); run(&now, 1);
    CHECK(update_state() == UP_ERROR && strstr(update_message(), "damaged"), "повреждённый файл отклонён по SHA-256");
    srv_corrupt = 0;

    /* уже последняя версия */
    update_init(); make_manifest(1); update_check(); run(&now, 1);
    CHECK(update_state() == UP_UPTODATE, "build 1 - уже актуально");

    /* ошибки сети */
    srv_http = 404; update_check(); run(&now, 1);
    CHECK(update_state() == UP_ERROR, "HTTP 404 - ошибка");
    srv_http = 200; srv_down = 1; update_check();
    CHECK(update_state() == UP_ERROR && update_message()[0], "нет сети - ошибка");
    srv_down = 0;
    update_set_auto(0); CHECK(!update_auto(), "автопроверка выключается");
    return fails;
}
