#include "installer.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "ui.h"
#include "anim.h"
#include "../kernel/install.h"
#include "../drivers/ahci.h"

extern void sys_reboot(void);

enum { P_WELCOME, P_DISK, P_ACCOUNT, P_CONFIRM, P_PROGRESS, P_DONE, P_ERROR };
enum { B_NEXT = 1, B_BACK, B_CANCEL, B_RESTART, B_SW, B_DISK = 10, B_FIELD = 20 };
enum { F_NAME, F_USER, F_PASS, F_PASS2, F_HOST, NF };

typedef struct { char s[64]; int len; } field_t;
typedef struct { i32 x, y, w, h; int id; } hit_t;

static int  active, page, off, focus, disk_sel, agree, ndisks, run_pending;
static int  prog, prog_shown;
static u32  now, last;
static i32  mx, my; static int mbtn_prev;
static field_t fld[NF];
static const char *errmsg, *step_txt = "";
static char errbuf[96];
static hit_t hits[24]; static int nhits;
static i32 cx0, cy0, cw, ch;                       /* карточка */

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scopy(char *d, const char *s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }

static void goto_page(int p) { page = p; off = 1024; }

void installer_open(void)
{
    active = 1; page = P_WELCOME; off = 1024; focus = F_NAME; agree = 0; disk_sel = 0; errmsg = 0; prog = prog_shown = 0; run_pending = 0;
    for (int i = 0; i < NF; i++) { fld[i].s[0] = 0; fld[i].len = 0; }
    scopy(fld[F_HOST].s, "endixos", 64); fld[F_HOST].len = slen(fld[F_HOST].s);
    ndisks = ahci_init();
    if (ndisks > 4) ndisks = 4;
}
int installer_active(void) { return active; }

/* ---------- рисование ---------- */
static void add_hit(i32 x, i32 y, i32 w, i32 h, int id)
{
    if (nhits < 24) { hits[nhits].x = x; hits[nhits].y = y; hits[nhits].w = w; hits[nhits].h = h; hits[nhits].id = id; nhits++; }
}
static int ihov(i32 x, i32 y, i32 w, i32 h) { return mx >= x && mx < x + w && my >= y && my < y + h; }

static void txt(i32 x, i32 y, const char *s, u32 c, int st) { font_draw(x, y, s, c, st); }
static void ctxt(i32 x, i32 w, i32 y, const char *s, u32 c, int st) { font_draw(x + (w - font_width(s, st)) / 2, y, s, c, st); }

static i32 para(i32 x, i32 y, i32 w, const char *s, u32 c)       /* перенос по словам; возвращает y после текста */
{
    char ln[96]; int n = 0, lw = 0; i32 lh = font_height[FONT_REGULAR] + 4;
    while (*s) {
        char wd[48]; int k = 0;
        while (*s == ' ') s++;
        while (*s && *s != ' ' && k < 47) wd[k++] = *s++;
        wd[k] = 0;
        if (!k) break;
        int ww = font_width(wd, FONT_REGULAR), sp = n ? font_width(" ", FONT_REGULAR) : 0;
        if (n && lw + sp + ww > w) { ln[n] = 0; txt(x, y, ln, c, FONT_REGULAR); y += lh; n = 0; lw = 0; sp = 0; }
        if (n + k + 2 < 96) { if (n) ln[n++] = ' '; for (int i = 0; i < k; i++) ln[n++] = wd[i]; lw += sp + ww; }
    }
    if (n) { ln[n] = 0; txt(x, y, ln, c, FONT_REGULAR); y += lh; }
    return y;
}

static void button(i32 x, i32 y, i32 w, int id, const char *label, int enabled, int primary)
{
    const i32 h = 32;
    int hv = enabled && ihov(x, y, w, h) && off < 100;
    u32 acc = theme_accent(), bg, fg;
    if (!enabled) { bg = theme_is_dark() ? RGB(70, 70, 74) : RGB(214, 214, 218); fg = RGB(150, 150, 154); }
    else if (primary) { bg = acc; fg = RGB(255, 255, 255); }
    else { bg = theme_is_dark() ? RGB(88, 88, 94) : RGB(226, 226, 230); fg = theme()->text; }
    fb_fill_round_rect(x, y, w, h, 8, bg, 255);
    if (hv) fb_blend_rect(x + 2, y + 2, w - 4, h - 4, primary ? RGB(255, 255, 255) : RGB(128, 128, 128), 40);
    ctxt(x, w, y + (h - font_height[FONT_REGULAR]) / 2, label, fg, FONT_REGULAR);
    if (enabled) add_hit(x, y, w, h, id);
}

static void field(i32 x, i32 y, i32 w, int i, const char *label, int mask)
{
    const theme_t *t = theme();
    txt(x, y, label, t->sub, FONT_REGULAR);
    i32 by = y + font_height[FONT_REGULAR] + 3, bh = 30;
    int f = focus == i;
    fb_fill_round_rect(x - (f ? 2 : 0), by - (f ? 2 : 0), w + (f ? 4 : 0), bh + (f ? 4 : 0), 8, f ? theme_accent() : t->sub, f ? 120 : 60);
    fb_fill_round_rect(x, by, w, bh, 7, theme_is_dark() ? RGB(40, 40, 44) : RGB(255, 255, 255), 255);
    char tmp[64]; int n = fld[i].len;
    if (mask) { for (int k = 0; k < n; k++) tmp[k] = '*'; tmp[n] = 0; } else scopy(tmp, fld[i].s, 64);
    i32 ty = by + (bh - font_height[FONT_REGULAR]) / 2, tw = font_width(tmp, FONT_REGULAR);
    txt(x + 8, ty, tmp, t->text, FONT_REGULAR);
    if (f && ((now / 50) & 1) == 0) fb_fill_rect(x + 9 + tw, by + 6, 2, bh - 12, t->text);
    add_hit(x, by, w, bh, B_FIELD + i);
}

static void draw_bg(void)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    int dk = theme_is_dark();
    for (i32 y = 0; y < H; y++) {
        u32 k = (u32)(y * 255 / (H - 1 ? H - 1 : 1));
        u32 r = dk ? 18 + k / 12 : 150 - k / 5, g = dk ? 20 + k / 10 : 170 - k / 6, b = dk ? 40 + k / 5 : 225 - k / 8;
        fb_fill_rect(0, y, W, 1, RGB(r, g, b));
    }
}

static int back_ok(void) { return page == P_DISK || page == P_ACCOUNT || page == P_CONFIRM; }

static void draw_page(i32 ox)
{
    const theme_t *t = theme();
    i32 x = cx0 + 32 + ox, w = cw - 64, y = cy0 + 28;
    char b[64];
    switch (page) {
    case P_WELCOME:
        ctxt(cx0 + ox, cw, y + 10, "Welcome to EndixOS", t->text, FONT_TITLE);
        y += 70;
        y = para(x, y, w, "This installer copies EndixOS to a SATA disk of this computer, creates your account and makes the disk bootable.", t->text);
        y += 10;
        y = para(x, y, w, "It takes about a minute. Everything on the chosen disk will be erased, so back up your files first.", t->sub);
        if (!inst_bootloader_available()) { y += 10; para(x, y, w, "Warning: this kernel was built without GRUB images, the installed disk will not be bootable.", RGB(214, 69, 65)); }
        break;
    case P_DISK:
        txt(x, y, "Choose a disk", t->text, FONT_TITLE);
        y += 56;
        if (ndisks == 0) {
            y = para(x, y, w, "No SATA disk was found. If this is a netbook, set the SATA mode to AHCI in BIOS (not IDE or Compatible). Run 'disks' in Terminal for details.", t->sub);
        }
        for (int i = 0; i < ndisks; i++) {
            const ahci_disk_t *d = ahci_disk(i);
            int sel = disk_sel == i, hv = ihov(cx0 + 32, y, w, 52) && off < 100;
            fb_fill_round_rect(x, y, w, 52, 10, sel ? theme_accent() : t->sub, sel ? 255 : (hv ? 70 : 40));
            u32 fg = sel ? RGB(255, 255, 255) : t->text;
            txt(x + 14, y + 8, d->model[0] ? d->model : "SATA disk", fg, FONT_BOLD);
            u32 mb = d->sectors / 2048, n = 0;
            if (mb >= 1024) { u32 gb = mb / 1024, fr = (mb % 1024) * 10 / 1024; b[n++] = (char)('0' + gb / 100 % 10); b[n++] = (char)('0' + gb / 10 % 10); b[n++] = (char)('0' + gb % 10); b[n++] = '.'; b[n++] = (char)('0' + fr); b[n++] = ' '; b[n++] = 'G'; b[n++] = 'B'; }
            else { b[n++] = (char)('0' + mb / 100 % 10); b[n++] = (char)('0' + mb / 10 % 10); b[n++] = (char)('0' + mb % 10); b[n++] = ' '; b[n++] = 'M'; b[n++] = 'B'; }
            b[n] = 0; char *p = b; while (p[0] == '0' && p[1] != '.' && p[1] != ' ') p++;
            txt(x + 14, y + 28, p, sel ? RGB(235, 235, 255) : t->sub, FONT_REGULAR);
            add_hit(cx0 + 32, y, w, 52, B_DISK + i);
            y += 60;
        }
        if (ndisks) para(x, cy0 + ch - 100, w, "The whole disk will be repartitioned and erased.", RGB(214, 69, 65));
        break;
    case P_ACCOUNT:
        txt(x, y - 4, "Create your account", t->text, FONT_TITLE);
        y += 48;
        {
            i32 rh = font_height[FONT_REGULAR] + 3 + 30 + 8, hw = (w - 16) / 2;
            field(x, y, w, F_NAME, "Full name", 0); y += rh;
            field(x, y, w, F_USER, "User name (a-z, 0-9, up to 8)", 0); y += rh;
            field(x, y, hw, F_PASS, "Password", 1);
            field(x + hw + 16, y, hw, F_PASS2, "Repeat password", 1); y += rh;
            field(x, y, w, F_HOST, "Computer name", 0);
        }
        if (errmsg) txt(x, cy0 + ch - 70, errmsg, RGB(214, 69, 65), FONT_REGULAR);
        break;
    case P_CONFIRM:
        txt(x, y - 4, "Ready to install", t->text, FONT_TITLE);
        y += 52;
        {
            const ahci_disk_t *d = ahci_disk(disk_sel);
            txt(x, y, "Disk:", t->sub, FONT_REGULAR); txt(x + 120, y, d && d->model[0] ? d->model : "SATA disk", t->text, FONT_REGULAR); y += 24;
            txt(x, y, "Account:", t->sub, FONT_REGULAR); txt(x + 120, y, fld[F_USER].s, t->text, FONT_REGULAR); y += 24;
            txt(x, y, "Computer:", t->sub, FONT_REGULAR); txt(x + 120, y, fld[F_HOST].s, t->text, FONT_REGULAR); y += 36;
        }
        fb_fill_round_rect(x, y, w, 56, 10, RGB(214, 69, 65), 40);
        para(x + 12, y + 8, w - 24, "All data on this disk will be permanently erased.", RGB(214, 69, 65));
        y += 72;
        ui_switch(x, y, agree);
        add_hit(x, y, 40, 24, B_SW);
        txt(x + 52, y + 3, "I understand, erase the disk", t->text, FONT_REGULAR);
        break;
    case P_PROGRESS:
        ctxt(cx0 + ox, cw, y + 20, "Installing EndixOS", t->text, FONT_TITLE);
        y += 120;
        fb_fill_round_rect(x, y, w, 10, 5, t->sub, 60);
        if (prog_shown > 0) fb_fill_round_rect(x, y, w * prog_shown / 100 > 10 ? w * prog_shown / 100 : 10, 10, 5, theme_accent(), 255);
        y += 24;
        ctxt(cx0 + ox, cw, y, step_txt, t->sub, FONT_REGULAR);
        y += 40;
        ctxt(cx0 + ox, cw, y, "Do not switch the computer off.", t->sub, FONT_REGULAR);
        break;
    case P_DONE:
        fb_fill_circle(cx0 + cw / 2 + ox, y + 50, 34, RGB(52, 199, 89));
        for (int k = 0; k <= 9; k++) fb_fill_circle(cx0 + cw / 2 + ox - 14 + k, y + 50 + k, 3, RGB(255, 255, 255));
        for (int k = 0; k <= 19; k++) fb_fill_circle(cx0 + cw / 2 + ox - 5 + k, y + 59 - k * 10 / 9 * 1, 3, RGB(255, 255, 255));
        ctxt(cx0 + ox, cw, y + 110, "EndixOS is installed", t->text, FONT_TITLE);
        para(x, y + 170, w, "Remove the USB drive and restart. The computer will start from the disk.", t->sub);
        break;
    case P_ERROR:
        fb_fill_circle(cx0 + cw / 2 + ox, y + 50, 34, RGB(214, 69, 65));
        ctxt(cx0 + ox, cw, y + 110, "Installation failed", t->text, FONT_TITLE);
        para(x, y + 170, w, errmsg ? errmsg : "Unknown error.", t->sub);
        break;
    }
}

void installer_draw(void)
{
    if (!active) return;
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    cw = W - 40 < 600 ? W - 40 : 600; ch = H - 20 < 440 ? H - 20 : 440;
    cx0 = (W - cw) / 2; cy0 = (H - ch) / 2;
    nhits = 0;
    draw_bg();
    fb_fill_round_rect(cx0 - 4, cy0 + 6, cw + 8, ch + 8, 20, 0, 70);            /* тень */
    fb_fill_round_rect(cx0, cy0, cw, ch, 16, theme()->body, 255);
    i32 ox = off * 60 / 1024;
    draw_page(ox);
    if (off > 0) fb_blend_rect(cx0 + 4, cy0 + 4, cw - 8, ch - 80, theme()->body, (u32)(off * 255 / 1024));   /* проявление страницы */
    /* кнопки */
    i32 by = cy0 + ch - 52;
    if (page == P_WELCOME) { button(cx0 + 24, by, 100, B_CANCEL, "Cancel", 1, 0); button(cx0 + cw - 144, by, 120, B_NEXT, "Continue", 1, 1); }
    else if (back_ok()) {
        button(cx0 + 24, by, 100, B_BACK, "Back", 1, 0);
        int ok = page == P_DISK ? ndisks > 0 : page == P_CONFIRM ? agree : 1;
        button(cx0 + cw - 144, by, 120, B_NEXT, page == P_CONFIRM ? "Install" : "Continue", ok, 1);
    } else if (page == P_DONE) button(cx0 + cw - 144, by, 120, B_RESTART, "Restart", 1, 1);
    else if (page == P_ERROR) { button(cx0 + 24, by, 100, B_CANCEL, "Close", 1, 0); button(cx0 + cw - 144, by, 120, B_BACK, "Try again", 1, 1); }
    /* точки шагов */
    if (page <= P_CONFIRM) for (int i = 0; i < 4; i++) fb_fill_circle(cx0 + cw / 2 - 30 + i * 20, by + 16, 4, i == page ? theme_accent() : theme()->sub);
}

/* ---------- установка ---------- */
static int dk_rd(void *c, u32 lba, u32 n, void *b) { return ahci_read((int)(long)c, lba, n, b); }
static int dk_wr(void *c, u32 lba, u32 n, const void *b) { return ahci_write((int)(long)c, lba, n, b); }

static void on_progress(int pct, const char *step)
{
    prog = pct; step_txt = step ? step : ""; prog_shown = pct;
    installer_draw();
    fb_flip();
}

static char cfgbuf[1400];
static int  cfgn;
static void cadd(const char *s) { while (*s && cfgn < (int)sizeof cfgbuf - 1) cfgbuf[cfgn++] = *s++; cfgbuf[cfgn] = 0; }
static void cnum(u32 v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) { char c[2] = { t[--n], 0 }; cadd(c); } }
static void centry(const char *name, u32 w, u32 h, int wide)
{
    cadd("menuentry \""); cadd(name); cadd("\" {\n    set gfxpayload="); cnum(w); cadd("x"); cnum(h); cadd("x32\n    multiboot /boot/endixos.elf");
    if (wide) cadd(" wide");
    cadd(" installed");
    cadd("\n    module /boot/endixos.elf endixos.elf\n    boot\n}\n");
}
static void make_cfg(void)
{
    cfgn = 0; cfgbuf[0] = 0;
    cadd("insmod all_video\ninsmod vbe\ninsmod vga\ninsmod multiboot\nset gfxmode=auto\nset timeout=2\nset default=0\n\n");
    u32 pw = fb_phys_width(), ph = fb_height();
    int wide = pw != fb_width();
    centry(wide ? "EndixOS (current screen mode, widescreen)" : "EndixOS", pw, ph, wide);
    if (!(pw == 800 && ph == 600 && !wide)) centry("EndixOS (800x600)", 800, 600, 0);
    if (!(pw == 1024 && ph == 768)) centry("EndixOS (1024x768, safe)", 1024, 768, 0);
}

static void run_install(void)
{
    static blk_t d;
    const ahci_disk_t *dk = ahci_disk(disk_sel);
    u32 klen = 0; const u8 *k = boot_kernel_image(&klen);
    const char *err = 0, *msg = "";
    if (!dk) { errmsg = "Disk disappeared."; goto fail; }
    if (!k || !klen) { errmsg = "The kernel image is not available. Start EndixOS from GRUB (the menu entry must have a 'module' line)."; goto fail; }
    d.rd = dk_rd; d.wr = dk_wr; d.ctx = (void *)(long)disk_sel; d.sectors = dk->sectors;
    make_cfg();
    inst_params_t p = { k, klen, cfgbuf, fld[F_USER].s, fld[F_NAME].s, fld[F_PASS].s, fld[F_HOST].s };
    if (inst_check_params(&p, &err)) { errmsg = err; goto fail; }
    prog = 0; prog_shown = 0;
    int r = inst_run(&d, &p, on_progress, &msg);
    if (r == 0) { goto_page(P_DONE); return; }
    scopy(errbuf, msg, 96); errmsg = errbuf;
fail:
    goto_page(P_ERROR);
}

/* ---------- ввод ---------- */
static int try_next(void)
{
    switch (page) {
    case P_WELCOME: goto_page(P_DISK); return 1;
    case P_DISK: if (ndisks > 0) goto_page(P_ACCOUNT); return 1;
    case P_ACCOUNT: {
        const char *err = 0;
        const char *fake = fld[F_PASS].s;
        int same = 1;
        if (fld[F_PASS].len != fld[F_PASS2].len) same = 0;
        else for (int i = 0; i < fld[F_PASS].len; i++) if (fld[F_PASS].s[i] != fld[F_PASS2].s[i]) same = 0;
        u8 dummy = 0;
        inst_params_t p = { &dummy, 1, "", fld[F_USER].s, fld[F_NAME].s, fake, fld[F_HOST].s };
        if (inst_check_params(&p, &err)) {
            /* проверка без ядра: пропускаем только жалобы на образ ядра */
            errmsg = err;
            return 1;
        }
        if (!same) { errmsg = "The passwords do not match."; return 1; }
        errmsg = 0; goto_page(P_CONFIRM); return 1;
    }
    case P_CONFIRM: if (agree) { goto_page(P_PROGRESS); run_pending = 2; } return 1;
    }
    return 0;
}

static int act(int id)
{
    if (id >= B_DISK && id < B_DISK + 4) { disk_sel = id - B_DISK; return 1; }
    if (id >= B_FIELD && id < B_FIELD + NF) { focus = id - B_FIELD; return 1; }
    switch (id) {
    case B_NEXT: return try_next();
    case B_BACK:
        if (page == P_ERROR) { goto_page(P_CONFIRM); agree = 0; return 1; }
        if (page > P_WELCOME) { errmsg = 0; goto_page(page - 1); }
        return 1;
    case B_CANCEL: active = 0; return 1;
    case B_RESTART: sys_reboot(); return 1;
    case B_SW: agree = !agree; return 1;
    }
    return 0;
}

int installer_mouse(i32 x, i32 y, int btn)
{
    if (!active) return 0;
    int moved = x != mx || y != my;
    mx = x; my = y;
    int click = (btn & 1) && !mbtn_prev;
    mbtn_prev = btn & 1;
    if (click && off < 200)
        for (int i = nhits - 1; i >= 0; i--)
            if (x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y && y < hits[i].y + hits[i].h)
                return act(hits[i].id) | 1;
    return moved || click;
}

int installer_key(int c)
{
    if (!active) return 0;
    if (page == P_ACCOUNT) {
        field_t *f = &fld[focus];
        if (c == '\t') { focus = (focus + 1) % NF; return 1; }
        if (c == 0x101) { focus = (focus + NF - 1) % NF; return 1; }
        if (c == 0x102 || c == '\n') { if (c == '\n' && focus == NF - 1) return try_next(); focus = (focus + 1) % NF; return 1; }
        if (c == '\b' || c == 0x7f) { if (f->len) f->s[--f->len] = 0; errmsg = 0; return 1; }
        if (c >= 32 && c < 127 && f->len < (focus == F_USER ? 8 : focus == F_HOST ? 30 : focus == F_NAME ? 40 : 60)) {
            if (focus == F_USER && c >= 'A' && c <= 'Z') c += 32;
            f->s[f->len++] = (char)c; f->s[f->len] = 0; errmsg = 0;
            return 1;
        }
    }
    if (c == '\n') {
        if (page == P_DONE) { sys_reboot(); return 1; }
        if (page == P_ERROR) return act(B_BACK);
        return try_next();
    }
    if (c == ' ' && page == P_CONFIRM) { agree = !agree; return 1; }
    if (page == P_DISK && ndisks > 1) {
        if (c == 0x101 && disk_sel > 0) { disk_sel--; return 1; }
        if (c == 0x102 && disk_sel < ndisks - 1) { disk_sel++; return 1; }
    }
    if (c == 27) return page == P_WELCOME || page == P_ERROR ? act(B_CANCEL) : back_ok() ? act(B_BACK) : 0;
    return 0;
}

int installer_tick(u32 ticks)
{
    if (!active) return 0;
    u32 dt = ticks - last; last = ticks;
    if (dt > 20) dt = 20;
    int redraw = 0;
    if (now / 50 != ticks / 50) redraw = 1;              /* мигание курсора */
    now = ticks;
    if (off > 0) { off = anim_approach(off, 0, dt * 2); redraw = 1; }
    if (page == P_PROGRESS && run_pending) {
        if (--run_pending == 0) { run_install(); redraw = 1; }   /* сначала два кадра с пустой полосой, потом блокирующая установка */
        else redraw = 1;
    }
    return redraw;
}
