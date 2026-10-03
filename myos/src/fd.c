#include "fd.h"
#include "fat16.h"
#include "tty.h"

static struct fd_entry table[FD_MAX];

int fd_open(const char* path) {
    struct fat16_dirent de;
    if (fat16_find(path, &de) != 0) return -1;
    if (de.attr & FAT16_ATTR_DIR) return -1;

    for (int i = 0; i < FD_MAX; i++) {
        if (!table[i].used) {
            table[i].used   = 1;
            table[i].de     = de;
            table[i].offset = 0;
            return i;
        }
    }
    return -1;
}

uint32_t fd_read(int fd, void* buf, uint32_t n) {
    if (fd < 0 || fd >= FD_MAX || !table[fd].used) return 0;
    struct fd_entry* e = &table[fd];

    if (e->offset >= e->de.size) return 0;
    uint32_t remain = e->de.size - e->offset;
    if (n > remain) n = remain;

    /* Читаем весь файл и копируем нужный кусок — просто, но неэффективно.
       Файлы у нас маленькие, это ок. */
    static uint8_t tmp[65536];
    uint32_t total = fat16_read(&e->de, tmp, sizeof(tmp));
    if (e->offset >= total) return 0;
    uint32_t avail = total - e->offset;
    if (n > avail) n = avail;

    uint8_t* dst = (uint8_t*)buf;
    for (uint32_t i = 0; i < n; i++) dst[i] = tmp[e->offset + i];
    e->offset += n;
    return n;
}

int fd_close(int fd) {
    if (fd < 0 || fd >= FD_MAX || !table[fd].used) return -1;
    table[fd].used = 0;
    return 0;
}

/* readdir: рекурсивно проходим root dir, отдаём index-ную запись */
static uint32_t rd_idx_target = 0;
static int      rd_found      = 0;
static struct ude rd_out;

static int rd_cb(const struct fat16_dirent* de, void* user) {
    (void)user;
    if (rd_found) return 1;
    if (rd_idx_target == 0) {
        fat16_name(de, rd_out.name);
        rd_out.is_dir = (de->attr & FAT16_ATTR_DIR) ? 1 : 0;
        rd_out.size   = de->size;
        rd_found      = 1;
        return 1;
    }
    rd_idx_target--;
    return 0;
}

int fd_readdir(uint32_t index, struct ude* out) {
    rd_idx_target = index;
    rd_found      = 0;
    fat16_ls(rd_cb, 0);
    if (!rd_found) return -1;
    for (uint32_t i = 0; i < sizeof(struct ude); i++)
        ((uint8_t*)out)[i] = ((uint8_t*)&rd_out)[i];
    return 0;
}
