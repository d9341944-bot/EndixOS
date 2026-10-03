#ifndef FAT16_H
#define FAT16_H
#include <stdint.h>

#define FAT16_MAX_NAME 13   /* 8+1+3+1 */

struct fat16_dirent {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_lo;
    uint32_t size;
} __attribute__((packed));

#define FAT16_ATTR_RO     0x01
#define FAT16_ATTR_HIDDEN 0x02
#define FAT16_ATTR_SYSTEM 0x04
#define FAT16_ATTR_VOLUME 0x08
#define FAT16_ATTR_DIR    0x10
#define FAT16_ATTR_ARCH   0x20
#define FAT16_ATTR_LFN    0x0F

int      fat16_init(void);

/* читает root dir, вызывает callback для каждой записи. cb возвращает:
   - 0: продолжать
   - 1: остановиться */
typedef int (*fat16_ls_cb)(const struct fat16_dirent* de, void* user);
void     fat16_ls(fat16_ls_cb cb, void* user);

/* Найти файл в корне, вернуть 0 при успехе. */
int      fat16_find(const char* name, struct fat16_dirent* out);

/* Прочитать файл целиком. Возвращает реальное число прочитанных байт. */
uint32_t fat16_read(const struct fat16_dirent* de, uint8_t* buf, uint32_t max);

/* Запись */
int      fat16_create(const char* name);
uint32_t fat16_write(const char* name, const uint8_t* buf, uint32_t size, uint32_t offset);
int      fat16_delete(const char* name);

/* утилита: форматирование имени 8.3 в "NAME.EXT" */
void     fat16_name(const struct fat16_dirent* de, char* out /* >= 13 */);

#endif
