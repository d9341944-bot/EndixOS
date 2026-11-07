#ifndef ENDIX_BLK_H
#define ENDIX_BLK_H
#include "types.h"

/* Блочное устройство: диск целиком (сектора по 512 байт). count <= 64 за один вызов, 0 - успех. */
typedef struct blk {
    int (*rd)(void *ctx, u32 lba, u32 count, void *buf);
    int (*wr)(void *ctx, u32 lba, u32 count, const void *buf);
    void *ctx;
    u32 sectors;
} blk_t;

/* ---- таблица разделов MBR ---- */
/* Пишет в MBR один основной раздел (загрузочный код и подпись диска в первых 446 байтах не трогает). */
int  mbr_set_single(blk_t *b, u32 start, u32 size, u8 type);
/* Копирует загрузочный код (первые 440 байт) в MBR, таблица разделов остаётся. */
int  mbr_set_code(blk_t *b, const u8 *code440);
int  mbr_get_part(blk_t *b, int n, u8 *type, u32 *start, u32 *size);   /* n = 0..3, 0 - успех, раздел существует */

/* ---- FAT32 ---- */
typedef struct {
    blk_t *b;
    u32 start, total, rsvd, fatsz, spc, root, data, clusters, next_free;
    u32 fat_lba_cached; u8 fat_cache[512]; int fat_dirty;
    u16 date, time;
} fat_t;

int  fat_format(blk_t *b, u32 start, u32 sectors, const char *label, fat_t *out);   /* создаёт пустую ФС и открывает её */
int  fat_mount(blk_t *b, u32 start, fat_t *f);
int  fat_mkdir(fat_t *f, const char *path);                        /* "/BOOT/GRUB": создаёт нет, уже есть - успех */
int  fat_write(fat_t *f, const char *path, const void *data, u32 len);   /* создаёт или заменяет файл */
int  fat_read(fat_t *f, const char *path, void *buf, u32 cap);     /* длина файла или -1 */
int  fat_sync(fat_t *f);
/* Перебор каталога (только чтение): cb(ctx, имя, короткое имя 8.3, размер, каталог ли); 0 из cb - продолжать.
 * Длинные имена (LFN) собираются, неASCII-символы заменяются на '?'. Возвращает число записей или -1. */
typedef int (*fat_list_cb)(void *ctx, const char *name, const char *alias, u32 size, int is_dir);
int  fat_list(fat_t *f, const char *path, fat_list_cb cb, void *ctx);
#endif
