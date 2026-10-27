/* EndixOS v8 - ядро, точка входа на C. */
#include "../drivers/ata.h"
#include "../drivers/e1000.h"
#include "net.h"
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

static void update_time(void)
{
    rtc_time_t t;
    rtc_read(&t);
    wm_set_time(t.hour, t.min, t.day, t.month, t.year);
}

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

    gdt_init();
    idt_init();
    pic_init();
    serial_puts("GDT, IDT, PIC ready\n");

    sysinfo_t info;
    cpu_brand(info.cpu);
    info.ram_mb = (mbi->flags & 1u) ? mbi->mem_upper / 1024 + 1 : 0;
    info.width  = mbi->framebuffer_width;
    info.height = mbi->framebuffer_height;

    wm_init(&info);
    update_time();
    if (ata_init()) prefs_init(ata_read, ata_write);      /* сохранённые настройки с виртуального диска */

    if (e1000_init()) { u8 mac[6]; e1000_mac(mac); net_init(mac, e1000_send); }   /* сеть: DHCP стартует сам */

    ps2_set_bounds(info.width, info.height);
    pit_init();
    ps2_init();

    wm_render();
    fb_flip();
    serial_puts("Desktop up, entering main loop\n");

    __asm__ volatile("sti");

    u32 next_second = pit_ticks() + PIT_HZ;
    for (;;) {
        int dirty = 0;
        event_t e;

        while (event_pop(&e))
            wm_handle_event(&e);

        u32 now = pit_ticks();
        if ((i32)(now - next_second) >= 0) {
            next_second = now + PIT_HZ;
            wm_set_uptime(now / PIT_HZ);
            rtc_time_t t;
            rtc_read(&t);
            if (wm_set_time(t.hour, t.min, t.day, t.month, t.year))
                dirty = 1;
        }

        e1000_poll(net_rx);
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
