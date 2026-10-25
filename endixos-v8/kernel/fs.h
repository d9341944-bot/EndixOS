#ifndef ENDIX_FS_H
#define ENDIX_FS_H

#include "types.h"

/* Файловая система в оперативной памяти: дерево папок и файлов.
 * Узел определяется номером (int). Корень "/" всегда узел 0. */

#define FS_MAX_NODES 160
#define FS_NAME_MAX  32
#define FS_POOL_SIZE (128 * 1024)
#define FS_FILE_MAX  (16 * 1024)

#define FS_EEXIST    -1      /* имя уже занято */
#define FS_EINVAL    -2      /* неверное имя или узел */
#define FS_ENOSPC    -3      /* нет места */
#define FS_ENOTEMPTY -4      /* папка не пуста */
#define FS_EPERM     -5      /* системный узел (корень, домашняя папка) */
#define FS_ENOENT    -6      /* не найдено */

void fs_init(void);

int  fs_root(void);
int  fs_home(void);                                 /* /home/endix */

int  fs_valid(int node);
int  fs_is_dir(int node);
const char *fs_name(int node);
int  fs_parent(int node);                           /* -1 у корня */
u32  fs_size(int node);
const u8 *fs_data(int node);

int  fs_lookup(int dir, const char *name);          /* узел или -1 */
int  fs_resolve(int cwd, const char *path);         /* /abs, rel, ., .., ~ ; узел или -1 */
void fs_path(int node, char *buf, int max);         /* полный путь */

/* Содержимое папки: сначала папки, затем файлы, по алфавиту. Возвращает число узлов. */
int  fs_list(int dir, int *out, int max);
int  fs_count(int dir);

/* Создание: возвращают номер узла или отрицательный код ошибки. */
int  fs_mkdir(int parent, const char *name);
int  fs_create(int parent, const char *name);

/* Запись: append = 0 заменяет содержимое, 1 дописывает в конец. 0 или код ошибки.
 * data не должен указывать внутрь fs_data() другого файла: пул может уплотниться
 * прямо во время записи. Для копирования файла сначала переложите данные в свой буфер. */
int  fs_write(int node, const char *data, u32 len, int append);

int  fs_remove(int node);                           /* файл или пустая папка */
int  fs_remove_tree(int node);                      /* вместе с содержимым */
int  fs_move(int node, int new_parent, const char *new_name);   /* перенос и переименование */

/* Свободное имя в папке: "base", "base 2", "base 3"... (расширение сохраняется). */
void fs_unique_name(int parent, const char *base, char *out);

u32  fs_used_bytes(void);

#endif
