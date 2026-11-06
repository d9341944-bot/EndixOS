/* EndixOS v8 - ядро, точка входа на C. */
#include "../drivers/ata.h"
#include "../drivers/e1000.h"
#include "../drivers/jme.h"
#include "../drivers/hda.h"
#include "../gui/control.h"
#include "../gui/photos.h"
#include "../gui/resolution.h"
#include "net.h"
#include "crypto.h"
#include "../gui/prefs.h"
#include "types.h"
#include "multiboot.h"
#include "io.h"
#include "gdt.h"
#include "idt.h"
#include "cpu.h"
#include "event.h"
#include "../drivers/serial.h"
#include "../drivers/pic.h"
#include "../drivers/pit.h"
#include "../drivers/ps2.h"
#include "../drivers/rtc.h"
#include "userdb.h"
#include "../gui/login.h"
#include "../gui/fb.h"
#include "../gui/wm.h"

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

/* Перезагрузка через контроллер клавиатуры (сигнал RESET). */
void sys_reboot(void)
{
    __asm__ volatile("cli");
    for (u32 i = 0; i < 100000 && (inb(0x64) & 0x02); i++)
        ;
    outb(0x64, 0xFE);
    halt_forever();
}

/* Выключение: порты ACPI-эмуляции QEMU/Bochs/VirtualBox, иначе останавливаем процессор. */
void sys_shutdown(void)
{
    __asm__ volatile("cli");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    halt_forever();
}

static inline u32 rdtsc_lo(void) { u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return lo; }

static int nic_jme;
static void net_stats_cb(u32 *rx, u32 *tx, u32 *tf, int *link) { int pr; if (nic_jme) { jme_stats(rx, tx, tf, &pr); *link = jme_link(); } else { e1000_stats(rx, tx, tf, &pr); *link = e1000_link(); } }

static void update_time(void)
{
    rtc_time_t t;
    rtc_read(&t);
    wm_set_time(t.hour, t.min, t.day, t.month, t.year);
}

/* Ядро как файл: GRUB подгружает его же вторым модулем, установщик копирует его на диск. */
static const u8 *kimg; static u32 kimg_len;
const u8 *boot_kernel_image(u32 *len) { *len = kimg_len; return kimg; }

void kernel_main(u32 magic, multiboot_info_t *mbi)
{
    serial_init();
    serial_puts("EndixOS v8: kernel started\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_puts("ERROR: not loaded by a Multiboot bootloader, magic=");
        serial_put_hex(magic);
        serial_puts("\n");
        halt_forever();
    }

    if (!(mbi->flags & MBI_FLAG_FRAMEBUFFER) || mbi->framebuffer_type != 1) {
        serial_puts("ERROR: no linear framebuffer provided by bootloader\n");
        serial_puts("Boot through GRUB (make run), plain 'qemu -kernel' cannot set a video mode.\n");
        halt_forever();
    }

    serial_puts("Framebuffer: ");
    serial_put_dec(mbi->framebuffer_width);
    serial_puts("x");
    serial_put_dec(mbi->framebuffer_height);
    serial_puts("x");
    serial_put_dec(mbi->framebuffer_bpp);
    serial_puts(" at ");
    serial_put_hex((u32)mbi->framebuffer_addr);
    serial_puts("\n");

    if (fb_init((u32)mbi->framebuffer_addr, mbi->framebuffer_pitch,
                mbi->framebuffer_width, mbi->framebuffer_height,
                mbi->framebuffer_bpp) != 0) {
        serial_puts("ERROR: unsupported framebuffer mode (need 32 bpp, <=1280x1024)\n");
        halt_forever();
    }

    if ((mbi->flags & 4u) && mbi->cmdline) {               /* параметр "wide": нетбук 1024x600 в режиме 800x600 */
        const char *c = (const char *)mbi->cmdline;
        for (; *c; c++)
            if (c[0] == 'w' && c[1] == 'i' && c[2] == 'd' && c[3] == 'e' && mbi->framebuffer_width == 800 && mbi->framebuffer_height == 600) {
                if (fb_set_logical_width(1024) == 0) serial_puts("wide mode: 1024x600 logical\n");
                break;
            }
    }
    if ((mbi->flags & 8u) && mbi->mods_count > 0) {
        const u32 *m = (const u32 *)mbi->mods_addr;
        if (m[1] > m[0]) { kimg = (const u8 *)m[0]; kimg_len = m[1] - m[0]; }
    }
    gdt_init();
    idt_init();
    pic_init();
    serial_puts("GDT, IDT, PIC ready\n");

    sysinfo_t info;
    cpu_brand(info.cpu);
    info.ram_mb = (mbi->flags & 1u) ? mbi->mem_upper / 1024 + 1 : 0;
    info.width  = fb_width();
    info.height = fb_height();

    wm_init(&info);
    update_time();
    int inst = 0;                                          /* "installed": запуск с диска, поставленного установщиком; здесь нужен вход */
    if ((mbi->flags & 4u) && mbi->cmdline)
        for (const char *c = (const char *)mbi->cmdline; *c; c++)
            if (c[0] == 'i' && c[1] == 'n' && c[2] == 's' && c[3] == 't' && c[4] == 'a' && c[5] == 'l' && c[6] == 'l' && c[7] == 'e' && c[8] == 'd') { inst = 1; break; }
    res_set_installed(inst);
    static userdb_t udb;
    if (inst && userdb_load(&udb) == 0) {
        wm_set_user(udb.user, udb.host);
        login_open(udb.user, udb.full, udb.salthash);
        serial_puts("login: user found on disk\n");
    } else if (inst) serial_puts("login: users.dat not found, starting without login\n");
    else wm_live_hint();                                   /* запуск с флешки: подсказка, где установщик */
    photos_scan();                                         /* фотографии для обоев с диска: /Pictures */
    if (ata_init()) prefs_init(ata_read, ata_write);      /* сохранённые настройки с виртуального диска */

    if (e1000_init()) { u8 mac[6]; e1000_mac(mac); net_init(mac, e1000_send); wm_set_net_stats(net_stats_cb); serial_puts("e1000: found, link "); serial_puts(e1000_link() ? "up\n" : "DOWN\n"); }
    else if (jme_init()) { u8 mac[6]; nic_jme = 1; jme_mac(mac); net_init(mac, jme_send); wm_set_net_stats(net_stats_cb); serial_puts("jme: found (JMicron Ethernet)\n"); }
    else serial_puts("e1000: not found on PCI\n");   /* сеть: DHCP стартует сам */

    ps2_set_bounds(info.width, info.height);
    pit_init();
    ps2_init();
    int audio = hda_init();
    serial_puts(audio ? "hda: audio output ready\n" : "hda: no HD Audio output found\n");

    wm_render();
    fb_flip();
    serial_puts("Desktop up, entering main loop\n");

    __asm__ volatile("sti");

    u32 next_second = pit_ticks() + PIT_HZ;
    int last_vol = -1;
    if (audio) hda_play_chime();                                   /* звук запуска */
    for (;;) {
        int dirty = 0;
        event_t e;

        while (event_pop(&e)) {
            rng_mix(rdtsc_lo() ^ (u32)e.a << 11 ^ (u32)e.b << 3);      /* энтропия для TLS: время и положение мыши */
            wm_handle_event(&e);
        }

        u32 now = pit_ticks();
        if ((i32)(now - next_second) >= 0) {
            next_second = now + PIT_HZ;
            wm_set_uptime(now / PIT_HZ);
            rtc_time_t t;
            rtc_read(&t);
            if (wm_set_time(t.hour, t.min, t.day, t.month, t.year))
                dirty = 1;
        }

        if (audio) {
            hda_poll(now, PIT_HZ);
            int v = cc_volume();
            if (v != last_vol) {                                   /* ползунок громкости: звук меняется, как в macOS слышен щелчок */
                hda_set_volume(v);
                if (last_vol >= 0) hda_play_pop();
                last_vol = v;
            }
        }
        e1000_poll(net_rx);
        jme_poll(net_rx);
        rng_mix(rdtsc_lo());
        net_tick(now);
        prefs_tick(now);
        if (wm_tick(now))
            dirty = 1;

        if (dirty)
            wm_invalidate();
        if (wm_frame(now))
            continue;

        /* Спим до следующего прерывания; cli/проверка/sti;hlt исключает потерю пробуждения. */
        __asm__ volatile("cli");
        if (!event_pending())
            __asm__ volatile("sti; hlt");
        else
            __asm__ volatile("sti");
    }
}
