#ifndef ENDIX_INSTALL_H
#define ENDIX_INSTALL_H
#include "blk.h"

typedef struct {
    const u8 *kernel; u32 kernel_len;             /* образ ядра (модуль GRUB) */
    const char *cfg;                              /* grub.cfg для установленной системы */
    const char *username;                         /* 1-8 символов a-z 0-9, имя домашней папки */
    const char *fullname;                         /* отображаемое имя, любое (латиница/кириллица, UTF-8) */
    const char *password;                         /* может быть пустым */
    const char *hostname;
} inst_params_t;

typedef void (*inst_progress_t)(int percent, const char *step);

const u8 *boot_kernel_image(u32 *len);            /* ядро как файл (модуль GRUB) или 0 */
int  inst_bootloader_available(void);             /* 1, если при сборке ядра были доступны образы GRUB */
int  inst_check_params(const inst_params_t *p, const char **err);   /* 0 - всё хорошо; err - сообщение для окна */
/* Полная установка на диск d (всё на нём будет стёрто). 0 - успех, иначе код ошибки; msg - что пошло не так. */
int  inst_run(blk_t *d, const inst_params_t *p, inst_progress_t progress, const char **msg);
/* Хэш пароля: "соль:хэш" в hex (32+1+64 символа), итерации SHA-256. */
void inst_hash_password(const char *pw, const u8 salt[16], char out[100]);
#endif
