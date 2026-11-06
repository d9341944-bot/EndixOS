#include "gui/photos.c"
#include "gui/settings.c"
#include "gui/wm.c"
extern int printf(const char *, ...); extern void *mmap(void *, unsigned long, int, int, int, long);
extern void *fopen(const char *, const char *); extern unsigned long fread(void *, unsigned long, unsigned long, void *); extern int fclose(void *);
extern int memcmp(const void *, const void *, unsigned long);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u8 disk[200000 * 512];
static int m_rd(void *c, u32 lba, u32 n, void *b) { (void)c; if ((u64)(lba + n) * 512 > sizeof disk) return -1; for (u32 i = 0; i < n * 512; i++) ((u8 *)b)[i] = disk[(u64)lba * 512 + i]; return 0; }
static int m_wr(void *c, u32 lba, u32 n, const void *b) { (void)c; if ((u64)(lba + n) * 512 > sizeof disk) return -1; for (u32 i = 0; i < n * 512; i++) disk[(u64)lba * 512 + i] = ((const u8 *)b)[i]; return 0; }
static u8 jbuf[400000], pbuf[400000];
static u32 *vram; static u32 T = 5000;
static void ticks(int n) { for (int i = 0; i < n; i++) { T += 2; now_ticks = T; wm_tick(T); } }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(2); }
static void click(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); ticks(40); }
int main(void)
{
    const char *S = "/tmp/claude-0/-home-claude/bf30dd6e-8990-5615-9d1e-ee55bc18bd5b/scratchpad";
    char path[200]; int jn = 0, pn = 0; void *f;

    { int i = 0; for (const char *q = S; *q; q++) path[i++] = *q; const char *t = "/forest.jpg"; for (; *t; t++) path[i++] = *t; path[i] = 0; f = fopen(path, "rb"); jn = (int)fread(jbuf, 1, sizeof jbuf, f); fclose(f);
      i = 0; for (const char *q = S; *q; q++) path[i++] = *q; t = "/small.png"; for (; *t; t++) path[i++] = *t; path[i] = 0; f = fopen(path, "rb"); pn = (int)fread(pbuf, 1, sizeof pbuf, f); fclose(f); }
    CHECK(jn > 1000 && pn > 100, "тестовые картинки прочитаны");
    static blk_t b; b.rd = m_rd; b.wr = m_wr; b.ctx = 0; b.sectors = 200000;
    static fat_t ff;
    CHECK(fat_format(&b, 0, 200000, "TEST", &ff) == 0, "FAT32 создана");
    CHECK(fat_mkdir(&ff, "/Pictures") == 0, "каталог Pictures");
    /* две пустышки, которые мы перепишем в LFN-записи, затем файл с коротким именем FORES~1.JPG */
    fat_write(&ff, "/Pictures/D1.TMP", "x", 1); fat_write(&ff, "/Pictures/D2.TMP", "x", 1);
    CHECK(fat_write(&ff, "/Pictures/FORES~1.JPG", jbuf, (u32)jn) == 0, "JPEG записан");
    CHECK(fat_write(&ff, "/Pictures/SMALL.PNG", pbuf, (u32)pn) == 0, "PNG записан");
    fat_write(&ff, "/Pictures/NOTE.TXT", "hi", 2);
    fat_sync(&ff);
    /* LFN "Forest Sunset.jpg" (17 символов) = 2 записи: 0x42 (символы 14..17), затем 0x01 (1..13) */
    {
        u32 dircl = 0, eo = 0; u8 sec[512];
        for (u32 lba = 0; lba < 200000 && !dircl; lba++) { m_rd(0, lba, 1, sec); for (u32 o = 0; o < 512; o += 32) if (!memcmp(sec + o, "D1      TMP", 11)) { dircl = lba + 1; eo = o; break; } }
        CHECK(dircl != 0, "запись D1.TMP найдена в каталоге");
        if (!dircl) return 1;
        m_rd(0, dircl - 1, 1, sec);
        const char *nm = "Forest Sunset.jpg"; u8 e[2][32]; for (int k = 0; k < 2; k++) for (int i = 0; i < 32; i++) e[k][i] = 0xFF;
        static const u8 pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
        for (int part = 0; part < 2; part++) {
            u8 *x = e[part]; int seq = part == 0 ? 2 : 1;
            x[0] = (u8)(seq | (part == 0 ? 0x40 : 0)); x[11] = 0x0F; x[12] = 0; x[13] = 0; x[26] = 0; x[27] = 0;
            for (int i = 0; i < 13; i++) {
                int idx = (seq - 1) * 13 + i; int ch = idx < 17 ? nm[idx] : (idx == 17 ? 0 : 0xFFFF);
                x[pos[i]] = (u8)ch; x[pos[i] + 1] = ch == 0xFFFF ? 0xFF : 0;
            }
        }
        for (int i = 0; i < 32; i++) { sec[eo + i] = e[0][i]; sec[eo + 32 + i] = e[1][i]; }
        m_wr(0, dircl - 1, 1, sec);
    }
    CHECK(photos_scan_volume(&b, 0) == 2, "найдены две фотографии (txt пропущен)");
    CHECK(!strcmp(photos_name(0), "Forest Sunset"), "длинное имя из LFN (без расширения)");
    CHECK(!strcmp(photos_name(1), "SMALL"), "короткое имя");
    static u32 out[100 * 62];
    CHECK(photos_paint(0, out, 100, 62) == 1, "миниатюра JPEG декодирована");
    u32 top = out[2 * 100 + 50], bot = out[58 * 100 + 50];
    CHECK((top >> 16 & 255) < 120 && (top & 255) > 150, "верх миниатюры синий (небо)");
    CHECK(bot != top, "низ отличается от верха");
    CHECK(photos_paint(1, out, 100, 62) == 1, "маленький PNG растянут");
    CHECK(((out[31 * 100 + 50]) >> 16 & 255) > 230, "центр PNG светлый (белый квадрат)");
    CHECK(photos_paint(5, out, 100, 62) == 0, "несуществующая");
    /* интеграция: Settings */
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    open_window(APP_SETTINGS); ticks(60);
    i32 ox = wins[APP_SETTINGS].x, oy = wins[APP_SETTINGS].y + TITLE_H;
    page = PAGE_PHOTOS; wm_render(); wm_render(); wm_render();
    CHECK(pthumb_ok[0] == 1 && pthumb_ok[1] == 1, "миниатюры загружены (по одной за кадр)");
    i32 tx, ty; thumb_pos(0, &tx, &ty);
    click(ox + tx + 20, oy + ty + 20);
    CHECK(settings_wallpaper() == WALL_PHOTO0, "клик по фото ставит обои");
    u32 px = *(fb_buffer() + 100 * 1024 + 500);
    CHECK((px & 255) > 100, "обои на экране — фото (синее небо)");
    CHECK(wallpaper_name(WALL_PHOTO0 + 1)[0] == 'S', "wallpaper_name для фото");
    printf("fails=%d\n", fails); return fails;
}
