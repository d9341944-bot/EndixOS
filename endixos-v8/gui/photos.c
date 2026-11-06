#include "photos.h"
#include "img.h"
#include "../kernel/blk.h"
#include "../drivers/ahci.h"

typedef struct { char name[40]; char path[40]; u32 size; } photo_t;
static photo_t ph[PHOTO_MAX];
static int nph;
static blk_t vol_blk;                                    /* раздел, где лежат фотографии */
static u32 vol_start;
static int have_vol;

static u8  filebuf[PHOTO_FILE_MAX];
static u32 tmpbuf[1920 * 1080];                          /* промежуточный кадр перед обрезкой */

static int d_rd(void *c, u32 lba, u32 n, void *b) { return ahci_read((int)(long)c, lba, n, b); }
static int d_wr(void *c, u32 lba, u32 n, const void *b) { (void)c; (void)lba; (void)n; (void)b; return -1; }

static int lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int ext_ok(const char *a)                         /* .jpg .jpeg .png (без учёта регистра) */
{
    int n = 0; while (a[n]) n++;
    int d = n; while (d > 0 && a[d - 1] != '.') d--;
    if (d == 0) return 0;
    const char *e = a + d;
    static const char *const ok[3] = { "jpg", "jpeg", "png" };
    for (int i = 0; i < 3; i++) {
        int k = 0;
        while (ok[i][k] && lower(e[k]) == ok[i][k]) k++;
        if (!ok[i][k] && !e[k]) return 1;
    }
    return 0;
}

static int on_entry(void *ctx, const char *name, const char *alias, u32 size, int is_dir)
{
    (void)ctx;
    if (is_dir || nph >= PHOTO_MAX || size == 0 || size > PHOTO_FILE_MAX || !ext_ok(name)) return nph >= PHOTO_MAX;
    photo_t *p = &ph[nph];
    int n = 0, last_dot = -1;
    for (; name[n] && n < 39; n++) if (name[n] == '.') last_dot = n;
    int len = last_dot > 0 ? last_dot : n;                /* имя без расширения */
    for (int i = 0; i < len && i < 39; i++) p->name[i] = name[i];
    p->name[len < 39 ? len : 39] = 0;
    const char *pre = "/Pictures/";
    int k = 0;
    for (; pre[k]; k++) p->path[k] = pre[k];
    for (int i = 0; alias[i] && k < 39; i++) p->path[k++] = alias[i];
    p->path[k] = 0;
    p->size = size;
    nph++;
    return 0;
}

int photos_scan_volume(blk_t *b, u32 start)
{
    static fat_t f;
    nph = 0; have_vol = 0;
    if (fat_mount(b, start, &f) != 0) return 0;
    fat_list(&f, "/Pictures", on_entry, 0);
    if (nph) { vol_blk = *b; vol_start = start; have_vol = 1; }
    return nph;
}

int photos_scan(void)
{
    static blk_t b;
    int n = ahci_init();
    for (int i = 0; i < n; i++) {
        const ahci_disk_t *d = ahci_disk(i);
        if (!d) continue;
        b.rd = d_rd; b.wr = d_wr; b.ctx = (void *)(long)i; b.sectors = d->sectors;
        u8 type; u32 start, size;
        if (mbr_get_part(&b, 0, &type, &start, &size) != 0 || (type != 0x0C && type != 0x0B)) continue;
        if (photos_scan_volume(&b, start) > 0) return nph;
    }
    return 0;
}

int photos_count(void) { return nph; }
const char *photos_name(int k) { return k >= 0 && k < nph ? ph[k].name : ""; }

int photos_paint(int k, u32 *dst, u32 w, u32 h)
{
    static fat_t f;
    if (k < 0 || k >= nph || !have_vol || w == 0 || h == 0) return 0;
    if (fat_mount(&vol_blk, vol_start, &f) != 0) return 0;
    int len = fat_read(&f, ph[k].path, filebuf, PHOTO_FILE_MAX);
    if (len <= 0) return 0;
    return photos_paint_mem(filebuf, (u32)len, dst, w, h);
}

int photos_paint_mem(const u8 *filebuf, u32 len_, u32 *dst, u32 w, u32 h)
{
    int len = (int)len_;
    if (w == 0 || h == 0) return 0;
    int iw, ih;
    if (!img_info(filebuf, (u32)len, &iw, &ih) || iw < 1 || ih < 1) return 0;
    /* размер, при котором картинка закрывает весь dst (cover), и не больше исходного */
    u32 dw, dh;
    if ((u32)iw >= w && (u32)ih >= h) {
        u32 sx = (w * 1024 + (u32)iw - 1) / (u32)iw, sy = (h * 1024 + (u32)ih - 1) / (u32)ih;      /* масштаб в 1/1024 */
        u32 s = sx > sy ? sx : sy;
        dw = ((u32)iw * s + 1023) / 1024; dh = ((u32)ih * s + 1023) / 1024;
        if (dw < w) dw = w;
        if (dh < h) dh = h;
        if (dw > (u32)iw) dw = (u32)iw;
        if (dh > (u32)ih) dh = (u32)ih;
    } else { dw = (u32)iw; dh = (u32)ih; }                /* меньше экрана: декодируем как есть, растянем ниже */
    if (dw * dh > sizeof(tmpbuf) / sizeof(tmpbuf[0])) { dw = w; dh = h; if (dw > (u32)iw || dh > (u32)ih) return 0; }   /* слишком вытянутая панорама: растягиваем */
    if (!img_decode_ex(filebuf, (u32)len, (int)dw, (int)dh, tmpbuf, 40u * 1000 * 1000)) return 0;
    if (dw >= w && dh >= h) {                             /* обрезка по центру */
        u32 ox = (dw - w) / 2, oy = (dh - h) / 2;
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++) dst[y * w + x] = tmpbuf[(y + oy) * dw + x + ox];
    } else {                                              /* маленькая картинка: заполнение с ближайшим соседом */
        u32 s = (w * 1024 + dw - 1) / dw, sy = (h * 1024 + dh - 1) / dh;
        if (sy > s) s = sy;
        u32 vw = dw * s / 1024, vh = dh * s / 1024;
        u32 ox = vw > w ? (vw - w) / 2 : 0, oy = vh > h ? (vh - h) / 2 : 0;
        for (u32 y = 0; y < h; y++) {
            u32 syy = (y + oy) * 1024 / s; if (syy >= dh) syy = dh - 1;
            for (u32 x = 0; x < w; x++) {
                u32 sxx = (x + ox) * 1024 / s; if (sxx >= dw) sxx = dw - 1;
                dst[y * w + x] = tmpbuf[syy * dw + sxx];
            }
        }
    }
    return 1;
}
