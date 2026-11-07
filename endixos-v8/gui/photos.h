#ifndef ENDIX_PHOTOS_H
#define ENDIX_PHOTOS_H
#include "../kernel/types.h"
#include "../kernel/blk.h"

/* Обои-фотографии пользователя: JPEG/PNG из папки /Pictures на FAT32-разделе SATA-диска (раздел 0).
 * Файлы читаются при выборе обоев и показе миниатюр. JPEG только baseline (не прогрессивный), до 4096 px в ширину. */
#define PHOTO_MAX 8
#define PHOTO_FILE_MAX (3 * 1024 * 1024)        /* больше - файл пропускается */

int  photos_scan(void);                                  /* ищет фотографии на дисках; число найденных */
int  photos_scan_volume(blk_t *b, u32 start);            /* то же для уже известного раздела (для тестов) */
int  photos_count(void);
const char *photos_name(int k);                          /* имя файла без расширения */
int  photos_paint(int k, u32 *dst, u32 w, u32 h);        /* картинка на весь dst (заполнение с обрезкой по центру); 1 - успех */
int  photos_paint_mem(const u8 *data, u32 len, u32 *dst, u32 w, u32 h);   /* то же для JPEG/PNG в памяти */
#endif
