/* PS/2: контроллер 8042, клавиатура (скан-коды набора 1) и мышь (3-байтовые пакеты). */
#include "ps2.h"
#include "pic.h"
#include "serial.h"
#include "../kernel/io.h"
#include "../kernel/idt.h"
#include "../kernel/event.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define ST_OUT_FULL 0x01
#define ST_IN_FULL  0x02
#define ST_MOUSE    0x20

/* ---------- низкоуровневый обмен с контроллером ---------- */

static int wait_write(void)
{
    for (u32 i = 0; i < 200000; i++)
        if (!(inb(PS2_STATUS) & ST_IN_FULL))
            return 0;
    return -1;
}

static int wait_read(void)
{
    for (u32 i = 0; i < 200000; i++)
        if (inb(PS2_STATUS) & ST_OUT_FULL)
            return 0;
    return -1;
}

static void ctrl_cmd(u8 c)
{
    wait_write();
    outb(PS2_CMD, c);
}

static void dev_write(u8 d)
{
    wait_write();
    outb(PS2_DATA, d);
}

static int dev_read(void)
{
    if (wait_read() != 0)
        return -1;
    return inb(PS2_DATA);
}

static void flush(void)
{
    for (int i = 0; i < 64 && (inb(PS2_STATUS) & ST_OUT_FULL); i++)
        inb(PS2_DATA);
}

static int mouse_cmd(u8 c)
{
    ctrl_cmd(0xD4);                       /* следующий байт - устройству 2 (мыши) */
    dev_write(c);
    return dev_read();                    /* ожидаем ACK = 0xFA */
}

/* ---------- мышь ---------- */

static i32 mx, my, bound_w = 1024, bound_h = 768;
static u8  pkt[6];
static int pkt_len, pkt_size = 3;     /* 4 байта, если мышь с колесом (IntelliMouse) */

void ps2_set_bounds(u32 width, u32 height)
{
    bound_w = (i32)width;
    bound_h = (i32)height;
    mx = bound_w / 2;
    my = bound_h / 2;
}

/* ---------- тачпад Synaptics (абсолютный режим, 6-байтовые пакеты) ---------- */

static int tp_on, tp_speed = 50, tp_tap = 1, tp_nat = 1;
static u32 tp_pkts, tp_taps, tp_rtaps, tp_scrolls;
static u8  tp_id[3];
static struct { int down, lx, ly, dur, travel, maxf, lastf, sacc, rx, ry, btn; } tp;

void ps2_tp_set(int speed, int tap, int natural) { tp_speed = speed < 0 ? 0 : speed > 100 ? 100 : speed; tp_tap = tap ? 1 : 0; tp_nat = natural ? 1 : 0; }
void ps2_tp_get(int *speed, int *tap, int *natural) { *speed = tp_speed; *tap = tp_tap; *natural = tp_nat; }
int  ps2_tp_present(void) { return tp_on; }

static void tp_buttons(int btn)
{
    if (btn == tp.btn) return;
    tp.btn = btn;
    event_push(EV_MOUSE, mx, my, btn);
}

static i32 iabs(i32 v) { return v < 0 ? -v : v; }

static void tp_move(i32 dx, i32 dy)
{
    i32 mag = iabs(dx) + iabs(dy);
    i32 g = 6 + tp_speed * 24 / 100;                      /* пикселей на 64 единицы тачпада */
    if (mag > 200) g *= 2; else if (mag > 80) g = g * 3 / 2;
    tp.rx += dx * g; tp.ry += dy * g;
    i32 px = tp.rx / 64, py = tp.ry / 64;
    tp.rx -= px * 64; tp.ry -= py * 64;
    if (!px && !py) return;
    mx += px; my -= py;                                   /* Y тачпада растёт вверх */
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > bound_w - 1) mx = bound_w - 1;
    if (my > bound_h - 1) my = bound_h - 1;
    event_push(EV_MOUSE, mx, my, tp.btn);
}

void tp_packet(const u8 *b)
{
    tp_pkts++;
    i32 x = (i32)(((b[3] & 0x10) << 8) | ((b[1] & 0x0F) << 8) | b[4]);
    i32 y = (i32)(((b[3] & 0x20) << 7) | ((b[1] & 0xF0) << 4) | b[5]);
    i32 z = b[2];
    i32 w = ((b[0] & 0x30) >> 2) | ((b[0] & 0x04) >> 1) | ((b[3] & 0x04) >> 2);
    int left = (b[0] & 1) | ((b[0] ^ b[3]) & 1), right = (b[0] >> 1) & 1;   /* у клик-падов кнопка видна как разница */
    int present = z > 25;
    int fingers = (w == 0 || w == 1) ? 2 : 1;
    tp_buttons(left | right << 1);
    if (!present) {
        if (tp.down) {                                    /* палец поднят: может это был тап */
            if (tp_tap && tp.dur <= 25 && tp.travel < 150 && tp.maxf == 1) { tp_taps++; event_push(EV_MOUSE, mx, my, 1); event_push(EV_MOUSE, mx, my, tp.btn); }
            else if (tp_tap && tp.dur <= 30 && tp.travel < 200 && tp.maxf == 2) { tp_rtaps++; event_push(EV_MOUSE, mx, my, 2); event_push(EV_MOUSE, mx, my, tp.btn); }
        }
        tp.down = 0; tp.sacc = 0; tp.rx = tp.ry = 0;
        return;
    }
    if (!tp.down) { tp.down = 1; tp.lx = x; tp.ly = y; tp.dur = 0; tp.travel = 0; tp.maxf = fingers; tp.lastf = fingers; tp.sacc = 0; return; }
    tp.dur++;
    if (fingers > tp.maxf) tp.maxf = fingers;
    if (fingers != tp.lastf) { tp.lastf = fingers; tp.lx = x; tp.ly = y; return; }   /* число пальцев сменилось: координата скачет */
    i32 dx = x - tp.lx, dy = y - tp.ly;
    tp.lx = x; tp.ly = y;
    tp.travel += iabs(dx) + iabs(dy);
    if (fingers == 1) { tp_move(dx, dy); return; }
    tp.sacc += tp_nat ? -dy : dy;                         /* палец вверх (dy > 0): классика - вверх, естественная - вниз */
    while (tp.sacc >= 90) { tp.sacc -= 90; tp_scrolls++; event_push(EV_WHEEL, 1, 0, 0); }
    while (tp.sacc <= -90) { tp.sacc += 90; tp_scrolls++; event_push(EV_WHEEL, -1, 0, 0); }
}

void ps2_tp_diag(char *out, int cap)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    const char *h = tp_on ? "Synaptics touchpad, id " : "No Synaptics touchpad (standard PS/2 mouse)";
    while (*h && n < cap - 40) out[n++] = *h++;
    if (tp_on) {
        for (int i = 0; i < 3; i++) { out[n++] = hex[tp_id[i] >> 4]; out[n++] = hex[tp_id[i] & 15]; if (i < 2) out[n++] = '.'; }
        const char *k[4] = { ", packets ", ", taps ", ", right taps ", ", scroll steps " };
        u32 v[4] = { tp_pkts, tp_taps, tp_rtaps, tp_scrolls };
        for (int i = 0; i < 4; i++) {
            const char *p = k[i]; while (*p && n < cap - 14) out[n++] = *p++;
            char t[11]; int m = 0; u32 q = v[i]; if (!q) t[m++] = '0';
            while (q) { t[m++] = (char)('0' + q % 10); q /= 10; }
            while (m && n < cap - 2) out[n++] = t[--m];
        }
    }
    out[n] = 0;
}

static int syn_send(u8 arg)                              /* команда Synaptics: 4 пары (E8, 2 бита) */
{
    for (int i = 6; i >= 0; i -= 2)
        if (mouse_cmd(0xE8) != 0xFA || mouse_cmd((u8)((arg >> i) & 3)) != 0xFA) return 0;
    return 1;
}

static int syn_detect(void)
{
    if (!syn_send(0x00) || mouse_cmd(0xE9) != 0xFA) return 0;
    int a = dev_read(), b = dev_read(), c = dev_read();
    if (a < 0 || b != 0x47) { flush(); return 0; }
    tp_id[0] = (u8)a; tp_id[1] = (u8)b; tp_id[2] = (u8)c;
    if (!syn_send(0xC5) || mouse_cmd(0xF3) != 0xFA || mouse_cmd(0x14) != 0xFA) return 0;   /* абсолютный режим, W, высокая частота, без жестов самого тачпада */
    return 1;
}

static void mouse_byte(u8 b)
{
    if (tp_on) {
        if (pkt_len == 0 && (b & 0xC8) != 0x80) return;
        pkt[pkt_len++] = b;
        if (pkt_len == 4 && (pkt[3] & 0xC8) != 0xC0) { pkt_len = 0; return; }
        if (pkt_len < 6) return;
        pkt_len = 0;
        tp_packet(pkt);
        return;
    }
    if (pkt_len == 0 && !(b & 0x08))
        return;                           /* потеряна синхронизация: первый байт всегда с битом 3 */
    pkt[pkt_len++] = b;
    if (pkt_len < pkt_size)
        return;
    pkt_len = 0;

    u8 f = pkt[0];
    if (f & 0xC0)
        return;                           /* переполнение счётчиков: пакет отбрасываем */

    i32 dx = (i32)pkt[1] - (i32)((f << 4) & 0x100);
    i32 dy = (i32)pkt[2] - (i32)((f << 3) & 0x100);

    mx += dx;
    my -= dy;                             /* у мыши Y растёт вверх, у экрана - вниз */
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > bound_w - 1) mx = bound_w - 1;
    if (my > bound_h - 1) my = bound_h - 1;

    event_push(EV_MOUSE, mx, my, f & 7);
    if (pkt_size == 4) {
        i32 z = (i32)(pkt[3] & 0x0F);
        if (z & 8) z -= 16;                   /* знаковое 4-битное колесо */
        if (z) event_push(EV_WHEEL, z, 0, 0);
    }
}

/* ---------- клавиатура ---------- */

/* Скан-коды набора 1, US-раскладка, индекс = скан-код (0x00..0x39). */
static const char map_normal[58] =
    "\0\033" "1234567890-=" "\b\t" "qwertyuiop[]" "\n" "\0" "asdfghjkl;'`" "\0"
    "\\" "zxcvbnm,./" "\0" "*" "\0" " ";
static const char map_shift[58] =
    "\0\033" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n" "\0" "ASDFGHJKL:\"~" "\0"
    "|" "ZXCVBNM<>?" "\0" "*" "\0" " ";

/* Русская ЙЦУКЕН: кодпоинты Юникода по скан-коду (0 = как в US). Для букв хранится строчная. */
static const unsigned short ru_map[58] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', 0, 0,
    L'й',L'ц',L'у',L'к',L'е',L'н',L'г',L'ш',L'щ',L'з',L'х',L'ъ', 0, 0,
    L'ф',L'ы',L'в',L'а',L'п',L'р',L'о',L'л',L'д',L'ж',L'э', L'ё', 0,
    '\\', L'я',L'ч',L'с',L'м',L'и',L'т',L'ь',L'б',L'ю','.', 0, '*', 0, ' '
};
static char ru_sym_shift(u8 sc)              /* Shift + небуквенная клавиша в RU (№ заменён на #: его нет в шрифте) */
{
    static const char dig[] = "!\"#;%:?*()_+";      /* скан-коды 0x02..0x0D */
    if (sc >= 2 && sc <= 0x0D) return dig[sc - 2];
    if (sc == 0x2B) return '/';
    if (sc == 0x35) return ',';
    return 0;
}

static int k_shift, k_ctrl, k_caps, k_ext, k_win, k_alt, k_ru;

int kbd_layout(void) { return k_ru; }           /* 0 = EN, 1 = RU */
void kbd_set_layout(int ru) { k_ru = ru != 0; event_push(EV_KEY, KEY_LAYOUT, 0, 0); }

static void kbd_byte(u8 b)
{
    if (b == 0xE0 || b == 0xE1) {         /* расширенные клавиши (стрелки и т.п.) пока игнорируем */
        k_ext = 1;
        return;
    }
    int release = b & 0x80;
    u8 sc = b & 0x7F;

    if (k_ext) {
        k_ext = 0;
        if (sc == 0x5B || sc == 0x5C) {   /* клавиша Win (Super) */
            k_win = !release;
            return;
        }
        if (sc == 0x38) {                 /* правый Alt */
            k_alt = !release;
            if (!release && k_shift) kbd_set_layout(!k_ru);
            return;
        }
        if (sc == 0x1D) {                 /* правый Ctrl */
            k_ctrl = !release;
            return;
        }
        if (release)
            return;
        switch (sc) {                     /* стрелки и Delete */
        case 0x48: event_push(EV_KEY, KEY_UP,     k_ctrl, 0); break;
        case 0x50: event_push(EV_KEY, KEY_DOWN,   k_ctrl, 0); break;
        case 0x4B: event_push(EV_KEY, KEY_LEFT,   k_ctrl, 0); break;
        case 0x4D: event_push(EV_KEY, KEY_RIGHT,  k_ctrl, 0); break;
        case 0x53: event_push(EV_KEY, KEY_DELETE, k_ctrl, 0); break;
        case 0x49: event_push(EV_KEY, KEY_PGUP,   k_ctrl, 0); break;
        case 0x51: event_push(EV_KEY, KEY_PGDN,   k_ctrl, 0); break;
        case 0x47: event_push(EV_KEY, KEY_HOME,   k_ctrl, 0); break;
        case 0x4F: event_push(EV_KEY, KEY_END,    k_ctrl, 0); break;
        default: break;
        }
        return;
    }

    if (sc == 0x2A || sc == 0x36) {
        k_shift = !release;
        if (!release && k_alt) kbd_set_layout(!k_ru);     /* Alt+Shift: EN <-> RU */
        return;
    }
    if (sc == 0x38) { k_alt = !release; if (!release && k_shift) kbd_set_layout(!k_ru); return; }
    if (sc == 0x1D)               { k_ctrl  = !release; return; }
    if (release)
        return;
    if (sc == 0x3A) { k_caps = !k_caps; return; }
    if (k_win && sc == 0x26) {            /* Win+L: экран блокировки */
        event_push(EV_KEY, KEY_LOCK, 0, 0);
        return;
    }
    if (k_win && sc == 0x39) { kbd_set_layout(!k_ru); return; }   /* Win+Пробел */
    if (sc == 0x3C) {                     /* F2 */
        event_push(EV_KEY, KEY_F2, k_ctrl, 0);
        return;
    }
    if (sc == 0x3D) {                     /* F3: Mission Control */
        event_push(EV_KEY, KEY_F3, k_ctrl, 0);
        return;
    }
    if (sc >= sizeof(map_normal))
        return;

    if (k_ru && !k_ctrl && !k_alt && ru_map[sc]) {
        int cp = ru_map[sc];
        if (cp >= 0x430) {                              /* буква: Shift или Caps -> заглавная */
            if (k_shift ^ k_caps) cp = cp == 0x451 ? 0x401 : cp - 32;
        } else if (k_shift && ru_sym_shift(sc)) cp = (u8)ru_sym_shift(sc);
        else if (k_shift && sc == 0x29) cp = 0x401;
        event_push(EV_KEY, cp, 0, 0);
        return;
    }
    char c = map_normal[sc];
    if (c >= 'a' && c <= 'z') {
        if (k_shift ^ k_caps)
            c = (char)(c - 32);
    } else if (k_shift) {
        c = map_shift[sc];
    }
    if (c)
        event_push(EV_KEY, (u8)c, k_ctrl, 0);
}

/* ---------- обработчик IRQ1 и IRQ12 ---------- */

static void ps2_irq(void)
{
    /* Оба прерывания читают один порт, поэтому разбираем всё, что накопилось,
     * и по биту статуса решаем, чей это байт. */
    for (int i = 0; i < 64; i++) {
        u8 st = inb(PS2_STATUS);
        if (!(st & ST_OUT_FULL))
            break;
        u8 b = inb(PS2_DATA);
        if (st & ST_MOUSE)
            mouse_byte(b);
        else
            kbd_byte(b);
    }
}

void ps2_init(void)
{
    ctrl_cmd(0xAD);                       /* отключить клавиатурный порт */
    ctrl_cmd(0xA7);                       /* отключить мышиный порт */
    flush();

    ctrl_cmd(0x20);                       /* прочитать байт конфигурации */
    int cfg = dev_read();
    if (cfg < 0)
        cfg = 0;
    serial_puts("PS/2: config byte ");
    serial_put_hex((u32)cfg);
    serial_puts("\n");

    cfg |= 0x03;                          /* разрешить IRQ1 и IRQ12 */
    cfg |= 0x40;                          /* трансляция в набор 1 (на всякий случай) */
    cfg &= ~0x30;                         /* включить тактирование обоих портов */
    ctrl_cmd(0x60);
    dev_write((u8)cfg);

    ctrl_cmd(0xAE);                       /* включить клавиатурный порт */
    ctrl_cmd(0xA8);                       /* включить мышиный порт */

    dev_write(0xF4);                      /* клавиатура: включить сканирование */
    int ack_kbd = dev_read();

    int ack_def   = mouse_cmd(0xF6);      /* мышь: настройки по умолчанию */
    if (syn_detect()) { tp_on = 1; pkt_size = 6; }
    else {
        mouse_cmd(0xF6);                  /* после неудачной пробы вернуть настройки по умолчанию */
        /* колесо: магическая последовательность частот 200,100,80, затем запрос ID (3 - есть колесо) */
        mouse_cmd(0xF3); mouse_cmd(200);
        mouse_cmd(0xF3); mouse_cmd(100);
        mouse_cmd(0xF3); mouse_cmd(80);
        if (mouse_cmd(0xF2) == 0xFA && dev_read() == 3) pkt_size = 4;
    }
    int ack_start = mouse_cmd(0xF4);      /* мышь: начать передачу данных */
    serial_puts("PS/2: ack kbd=");
    serial_put_hex((u32)ack_kbd);
    serial_puts(" mouse defaults=");
    serial_put_hex((u32)ack_def);
    serial_puts(" mouse enable=");
    serial_put_hex((u32)ack_start);
    serial_puts("\n");

    flush();
    pkt_len = 0;

    irq_register(1, ps2_irq);
    irq_register(12, ps2_irq);
    pic_unmask(1);
    pic_unmask(12);
}
