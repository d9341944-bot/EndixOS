/* Минимальная FAT32: форматирование, каталоги, запись и чтение файлов с именами 8.3. Хватает для загрузчика и системных файлов. */
#include "blk.h"

#define SEC 512
static void zero(void *p, u32 n) { u8 *a = p; while (n--) *a++ = 0; }
static void cp(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static u32 get16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 get32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

#define EOC 0x0FFFFFF8u

/* ---- сектора раздела ---- */
static int rdsec(fat_t *f, u32 s, u32 n, void *buf) { return f->b->rd(f->b->ctx, f->start + s, n, buf); }
static int wrsec(fat_t *f, u32 s, u32 n, const void *buf) { return f->b->wr(f->b->ctx, f->start + s, n, buf); }

/* ---- FAT с кэшем одного сектора ---- */
static int fat_flush(fat_t *f)
{
    if (!f->fat_dirty) return 0;
    f->fat_dirty = 0;
    if (wrsec(f, f->fat_lba_cached, 1, f->fat_cache)) return -1;
    return wrsec(f, f->fat_lba_cached + f->fatsz, 1, f->fat_cache);        /* вторая копия */
}

static int fat_load(fat_t *f, u32 sector)
{
    if (f->fat_lba_cached == sector) return 0;
    if (fat_flush(f)) return -1;
    if (rdsec(f, sector, 1, f->fat_cache)) { f->fat_lba_cached = 0xFFFFFFFFu; return -1; }
    f->fat_lba_cached = sector;
    return 0;
}

static u32 fat_get(fat_t *f, u32 cl)
{
    if (fat_load(f, f->rsvd + cl / 128)) return 0x0FFFFFF7u;
    return get32(f->fat_cache + (cl % 128) * 4) & 0x0FFFFFFFu;
}

static int fat_set(fat_t *f, u32 cl, u32 v)
{
    if (fat_load(f, f->rsvd + cl / 128)) return -1;
    u8 *p = f->fat_cache + (cl % 128) * 4;
    put32(p, (get32(p) & 0xF0000000u) | (v & 0x0FFFFFFFu));
    f->fat_dirty = 1;
    return 0;
}

int fat_sync(fat_t *f) { return fat_flush(f); }

static u32 cl_lba(const fat_t *f, u32 cl) { return f->data + (cl - 2) * f->spc; }

/* Свободные кластеры подряд: n штук, начиная с next_free; возвращает первый или 0. */
static u32 alloc_run(fat_t *f, u32 n)
{
    u32 cl = f->next_free, run = 0, first = 0, scanned = 0;
    while (scanned < f->clusters + 2) {
        if (cl >= f->clusters + 2) { cl = 2; run = 0; }
        if (fat_get(f, cl) == 0) { if (!run) first = cl; if (++run == n) break; }
        else run = 0;
        cl++; scanned++;
    }
    if (run != n) return 0;
    for (u32 i = 0; i < n; i++) if (fat_set(f, first + i, i + 1 == n ? 0x0FFFFFFFu : first + i + 1)) return 0;
    f->next_free = first + n;
    return first;
}

static int free_chain(fat_t *f, u32 cl)
{
    while (cl >= 2 && cl < 0x0FFFFFF7u) {
        u32 nx = fat_get(f, cl);
        if (fat_set(f, cl, 0)) return -1;
        cl = nx;
    }
    return 0;
}

/* ---- форматирование ---- */
int fat_format(blk_t *b, u32 start, u32 sectors, const char *label, fat_t *f)
{
    if (sectors < 70000) return -1;
    zero(f, sizeof *f);
    f->b = b; f->start = start; f->total = sectors; f->rsvd = 32; f->date = 0x5C21; f->time = 0;
    f->spc = sectors < 532480 ? 1 : sectors < 16777216 ? 8 : sectors < 33554432 ? 16 : sectors < 67108864 ? 32 : 64;
    u32 fz = 1;
    for (;;) {
        u32 cl = (sectors - f->rsvd - 2 * fz) / f->spc, need = (cl + 2 + 127) / 128;
        if (need <= fz) { f->clusters = cl; break; }
        fz = need;
    }
    f->fatsz = fz; f->root = 2; f->data = f->rsvd + 2 * fz; f->next_free = 3; f->fat_lba_cached = 0xFFFFFFFFu;
    u8 s[SEC];
    zero(s, SEC);
    s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90; cp(s + 3, "ENDIXOS ", 8);
    put16(s + 11, SEC); s[13] = (u8)f->spc; put16(s + 14, f->rsvd); s[16] = 2;
    s[21] = 0xF8; put16(s + 24, 63); put16(s + 26, 255); put32(s + 28, start); put32(s + 32, sectors);
    put32(s + 36, fz); put32(s + 44, 2); put16(s + 48, 1); put16(s + 50, 6);
    s[64] = 0x80; s[66] = 0x29; put32(s + 67, 0x454E4458u ^ sectors);
    for (int i = 0; i < 11; i++) s[71 + i] = ' ';
    for (int i = 0; i < 11 && label && label[i]; i++) s[71 + i] = (u8)label[i];
    cp(s + 82, "FAT32   ", 8); s[510] = 0x55; s[511] = 0xAA;
    if (wrsec(f, 0, 1, s) || wrsec(f, 6, 1, s)) return -1;
    u8 fi[SEC]; zero(fi, SEC);
    put32(fi, 0x41615252u); put32(fi + 484, 0x61417272u); put32(fi + 488, f->clusters - 1); put32(fi + 492, 3); put32(fi + 508, 0xAA550000u);
    if (wrsec(f, 1, 1, fi) || wrsec(f, 7, 1, fi)) return -1;
    static u8 z[SEC * 64]; zero(z, sizeof z);
    for (u32 c = 0; c < 2; c++)
        for (u32 off = 0; off < fz; ) {
            u32 n = fz - off > 64 ? 64 : fz - off;
            if (wrsec(f, f->rsvd + c * fz + off, n, z)) return -1;
            off += n;
        }
    for (u32 off = 0; off < f->spc; ) {                                   /* пустой корень */
        u32 n = f->spc - off > 64 ? 64 : f->spc - off;
        if (wrsec(f, f->data + off, n, z)) return -1;
        off += n;
    }
    if (fat_set(f, 0, 0x0FFFFFF8u) || fat_set(f, 1, 0x0FFFFFFFu) || fat_set(f, 2, 0x0FFFFFFFu)) return -1;
    return fat_flush(f);
}

int fat_mount(blk_t *b, u32 start, fat_t *f)
{
    u8 s[SEC];
    zero(f, sizeof *f);
    f->b = b; f->start = start; f->fat_lba_cached = 0xFFFFFFFFu; f->date = 0x5C21;
    if (b->rd(b->ctx, start, 1, s) || s[510] != 0x55 || s[511] != 0xAA || get16(s + 11) != SEC || s[82] != 'F') return -1;
    f->spc = s[13]; f->rsvd = get16(s + 14); f->total = get32(s + 32); f->fatsz = get32(s + 36); f->root = get32(s + 44);
    if (!f->spc || !f->fatsz || f->total < f->rsvd + 2 * f->fatsz) return -1;
    f->data = f->rsvd + 2 * f->fatsz;
    f->clusters = (f->total - f->data) / f->spc;
    f->next_free = 3;
    return 0;
}

/* ---- имена 8.3 ---- */
static int short_name(const char *s, u32 len, u8 *out)
{
    for (int i = 0; i < 11; i++) out[i] = ' ';
    u32 i = 0, n = 0;
    while (i < len && s[i] != '.') {
        char c = s[i++];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (n >= 8 || c < 33 || c == '"' || c == '*' || c == '+' || c == ',' || c == '/' || c == ':' || c == ';' || c == '<' || c == '=' || c == '>' || c == '?' || c == '\\' || c == '[' || c == ']' || c == '|') return -1;
        out[n++] = (u8)c;
    }
    if (!n) return -1;
    if (i < len) {
        i++; u32 e = 0;
        while (i < len) {
            char c = s[i++];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if (e >= 3 || c < 33 || c == '.' || c == '/') return -1;
            out[8 + e++] = (u8)c;
        }
    }
    return 0;
}

/* Ищет запись name в каталоге dir; при успехе даёт сектор и смещение записи. */
static int dir_find(fat_t *f, u32 dir, const u8 *name, u32 *sec, u32 *off, u8 *ent)
{
    u8 buf[SEC];
    for (u32 cl = dir; cl >= 2 && cl < 0x0FFFFFF7u; cl = fat_get(f, cl))
        for (u32 s = 0; s < f->spc; s++) {
            u32 lba = cl_lba(f, cl) + s;
            if (rdsec(f, lba, 1, buf)) return -1;
            for (u32 o = 0; o < SEC; o += 32) {
                if (buf[o] == 0) return 1;                               /* дальше записей нет */
                if (buf[o] == 0xE5 || buf[o + 11] == 0x0F) continue;
                int same = 1;
                for (int k = 0; k < 11; k++) if (buf[o + k] != name[k]) { same = 0; break; }
                if (same) { *sec = lba; *off = o; if (ent) cp(ent, buf + o, 32); return 0; }
            }
        }
    return 1;
}

/* Добавляет запись в каталог dir (расширяет каталог при необходимости). */
static int dir_add(fat_t *f, u32 dir, const u8 *ent)
{
    u8 buf[SEC];
    u32 last = dir;
    for (u32 cl = dir; cl >= 2 && cl < 0x0FFFFFF7u; cl = fat_get(f, cl)) {
        last = cl;
        for (u32 s = 0; s < f->spc; s++) {
            u32 lba = cl_lba(f, cl) + s;
            if (rdsec(f, lba, 1, buf)) return -1;
            for (u32 o = 0; o < SEC; o += 32)
                if (buf[o] == 0 || buf[o] == 0xE5) { cp(buf + o, ent, 32); return wrsec(f, lba, 1, buf); }
        }
    }
    u32 nc = alloc_run(f, 1);
    if (!nc) return -1;
    u8 z[SEC]; zero(z, SEC);
    for (u32 s = 0; s < f->spc; s++) if (wrsec(f, cl_lba(f, nc) + s, 1, z)) return -1;
    if (fat_set(f, last, nc)) return -1;
    cp(z, ent, 32);
    return wrsec(f, cl_lba(f, nc), 1, z);
}

static void mkent(fat_t *f, u8 *e, const u8 *name, u8 attr, u32 cl, u32 size)
{
    zero(e, 32); cp(e, name, 11); e[11] = attr;
    put16(e + 20, cl >> 16); put16(e + 26, cl & 0xFFFF); put32(e + 28, size);
    put16(e + 14, f->time); put16(e + 16, f->date); put16(e + 18, f->date); put16(e + 22, f->time); put16(e + 24, f->date);
}

/* Разбирает путь: возвращает каталог последнего компонента (без него) и его имя. create_dirs=0: каталоги должны существовать. */
static int walk(fat_t *f, const char *path, u32 *dir, u8 *last)
{
    u32 d = f->root;
    const char *p = path;
    while (*p == '/') p++;
    for (;;) {
        u32 l = 0; while (p[l] && p[l] != '/') l++;
        if (!l) return -1;
        u8 nm[11];
        if (short_name(p, l, nm)) return -1;
        const char *q = p + l; while (*q == '/') q++;
        if (!*q) { *dir = d; cp(last, nm, 11); return 0; }
        u32 sec, off; u8 e[32];
        if (dir_find(f, d, nm, &sec, &off, e) != 0 || !(e[11] & 0x10)) return -1;
        d = get16(e + 20) << 16 | get16(e + 26);
        if (d < 2) d = f->root;
        p = q;
    }
}

int fat_mkdir(fat_t *f, const char *path)
{
    u32 dir; u8 nm[11], e[32]; u32 sec, off;
    if (walk(f, path, &dir, nm)) return -1;
    int r = dir_find(f, dir, nm, &sec, &off, e);
    if (r == 0) return (e[11] & 0x10) ? 0 : -1;
    if (r < 0) return -1;
    u32 cl = alloc_run(f, 1);
    if (!cl) return -1;
    u8 z[SEC]; zero(z, SEC);
    for (u32 s = 0; s < f->spc; s++) if (wrsec(f, cl_lba(f, cl) + s, 1, z)) return -1;
    u8 dot[11] = { '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }, dd[11] = { '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    mkent(f, z, dot, 0x10, cl, 0);
    mkent(f, z + 32, dd, 0x10, dir == f->root ? 0 : dir, 0);
    if (wrsec(f, cl_lba(f, cl), 1, z)) return -1;
    mkent(f, e, nm, 0x10, cl, 0);
    if (dir_add(f, dir, e)) return -1;
    return fat_flush(f);
}

int fat_write(fat_t *f, const char *path, const void *data, u32 len)
{
    u32 dir; u8 nm[11], e[32]; u32 sec, off;
    if (walk(f, path, &dir, nm)) return -1;
    int r = dir_find(f, dir, nm, &sec, &off, e);
    if (r < 0) return -1;
    if (r == 0) {                                                   /* заменяем: старый файл освобождаем, запись переиспользуем */
        if (e[11] & 0x10) return -1;
        u32 old = get16(e + 20) << 16 | get16(e + 26);
        if (old >= 2 && free_chain(f, old)) return -1;
    }
    u32 csz = f->spc * SEC, ncl = len ? (len + csz - 1) / csz : 0, first = 0;
    if (ncl) {
        first = alloc_run(f, ncl);
        if (!first) { f->next_free = 3; first = alloc_run(f, ncl); }
        if (!first) return -1;
        const u8 *src = data;
        u32 done = 0, lba = cl_lba(f, first), total_s = (len + SEC - 1) / SEC;
        static u8 tail[SEC * 64];
        while (done < total_s) {
            u32 n = total_s - done > 64 ? 64 : total_s - done;
            u32 bytes = n * SEC, have = len - done * SEC;
            if (have < bytes) { zero(tail, bytes); cp(tail, src + done * SEC, have); if (wrsec(f, lba + done, n, tail)) return -1; }
            else if (wrsec(f, lba + done, n, src + done * SEC)) return -1;
            done += n;
        }
    }
    mkent(f, e, nm, 0x20, first, len);
    if (r == 0) {
        u8 buf[SEC];
        if (rdsec(f, sec, 1, buf)) return -1;
        cp(buf + off, e, 32);
        if (wrsec(f, sec, 1, buf)) return -1;
    } else if (dir_add(f, dir, e)) return -1;
    return fat_flush(f);
}

int fat_read(fat_t *f, const char *path, void *buf, u32 cap)
{
    u32 dir; u8 nm[11], e[32]; u32 sec, off;
    if (walk(f, path, &dir, nm) || dir_find(f, dir, nm, &sec, &off, e) != 0 || (e[11] & 0x10)) return -1;
    u32 size = get32(e + 28), cl = get16(e + 20) << 16 | get16(e + 26), got = 0;
    if (size > cap) size = cap;
    u8 tmp[SEC];
    while (got < size && cl >= 2 && cl < 0x0FFFFFF7u) {
        for (u32 s = 0; s < f->spc && got < size; s++) {
            if (rdsec(f, cl_lba(f, cl) + s, 1, tmp)) return -1;
            u32 n = size - got < SEC ? size - got : SEC;
            cp((u8 *)buf + got, tmp, n); got += n;
        }
        cl = fat_get(f, cl);
    }
    return (int)got;
}

int fat_list(fat_t *f, const char *path, fat_list_cb cb, void *ctx)
{
    u32 dir = f->root;
    if (path[0] && !(path[0] == '/' && !path[1])) {
        u32 d, sec, off; u8 nm[11], e[32];
        if (walk(f, path, &d, nm) || dir_find(f, d, nm, &sec, &off, e) != 0 || !(e[11] & 0x10)) return -1;
        dir = get16(e + 20) << 16 | get16(e + 26);
        if (dir < 2) dir = f->root;
    }
    u8 buf[SEC];
    char lfn[260]; int have_lfn = 0, count = 0;
    for (u32 cl = dir; cl >= 2 && cl < 0x0FFFFFF7u; cl = fat_get(f, cl))
        for (u32 s = 0; s < f->spc; s++) {
            if (rdsec(f, cl_lba(f, cl) + s, 1, buf)) return -1;
            for (u32 o = 0; o < SEC; o += 32) {
                u8 *e = buf + o;
                if (e[0] == 0) return count;
                if (e[0] == 0xE5) { have_lfn = 0; continue; }
                if (e[11] == 0x0F) {                                    /* кусок длинного имени */
                    u32 seq = e[0] & 0x1F;
                    if (e[0] & 0x40) { for (int i = 0; i < 260; i++) lfn[i] = 0; have_lfn = 1; }
                    if (!have_lfn || seq < 1 || seq > 20) { have_lfn = 0; continue; }
                    static const u8 pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                    for (int i = 0; i < 13; i++) {
                        u32 c = get16(e + pos[i]);
                        if (c == 0 || c == 0xFFFF) continue;
                        lfn[(seq - 1) * 13 + (u32)i] = c < 128 ? (char)c : '?';
                    }
                    continue;
                }
                if ((e[11] & 0x08) || e[0] == '.') { have_lfn = 0; continue; }       /* метка тома, "." и ".." */
                char alias[13]; int n = 0;
                for (int i = 0; i < 8 && e[i] != ' '; i++) alias[n++] = (char)e[i];
                if (e[8] != ' ') { alias[n++] = '.'; for (int i = 8; i < 11 && e[i] != ' '; i++) alias[n++] = (char)e[i]; }
                alias[n] = 0;
                const char *name = have_lfn && lfn[0] ? lfn : alias;
                have_lfn = 0;
                count++;
                if (cb(ctx, name, alias, get32(e + 28), (e[11] & 0x10) != 0)) return count;
            }
        }
    return count;
}
