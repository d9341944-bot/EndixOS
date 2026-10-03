#ifndef FD_H
#define FD_H
#include <stdint.h>
#include "fat16.h"

#define FD_MAX 16

struct fd_entry {
    int                 used;
    struct fat16_dirent de;
    uint32_t            offset;
};

struct ude {
    char     name[13];
    uint8_t  is_dir;
    uint32_t size;
} __attribute__((packed));

int      fd_open(const char* path);          /* → fd или -1 */
uint32_t fd_read(int fd, void* buf, uint32_t n);
int      fd_close(int fd);

/* индекс в корне: 0,1,2... → 0 при успехе, -1 при выходе за границы */
int      fd_readdir(uint32_t index, struct ude* out);

#endif
