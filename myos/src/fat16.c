#include "fat16.h"
#include "ata.h"
#include "tty.h"
#include <stdint.h>

/* Прочитанные параметры BPB */
static uint32_t bytes_per_sector  = 0;
static uint32_t sectors_per_cluster = 0;
static uint32_t reserved_sectors  = 0;
static uint32_t num_fats          = 0;
static uint32_t root_entries      = 0;
static uint32_t fat_size_sectors  = 0;
static uint32_t root_dir_sectors  = 0;
static uint32_t fat_start_lba     = 0;
static uint32_t root_start_lba    = 0;
static uint32_t data_start_lba    = 0;

static int fat16_ok = 0;

/* Простой кэш одного сектора */
static uint8_t sector_buf[512];
static uint32_t sector_buf_lba = 0xFFFFFFFF;
static int      sector_buf_valid = 0;

static int rd_sector(uint32_t lba, uint8_t* out) {
    if (sector_buf_valid && sector_buf_lba == lba) {
        for (int i = 0; i < 512; i++) out[i] = sector_buf[i];
        return 0;
    }
    if (ata_read_sector(lba, sector_buf) != 0) return -1;
    sector_buf_lba   = lba;
    sector_buf_valid = 1;
    for (int i = 0; i < 512; i++) out[i] = sector_buf[i];
    return 0;
}

int fat16_init(void) {
    uint8_t bs[512];
    if (ata_read_sector(0, bs) != 0) return -1;

    /* boot sector signature */
    if (bs[510] != 0x55 || bs[511] != 0xAA) {
        tty_puts("[fat16] bad boot signature\n");
        return -1;
    }

    bytes_per_sector    = bs[11] | ((uint16_t)bs[12] << 8);
    sectors_per_cluster = bs[13];
    reserved_sectors    = bs[14] | ((uint16_t)bs[15] << 8);
    num_fats            = bs[16];
    root_entries        = bs[17] | ((uint16_t)bs[18] << 8);
    fat_size_sectors    = bs[22] | ((uint16_t)bs[23] << 8);

    if (bytes_per_sector != 512) {
        tty_puts("[fat16] sector size != 512\n");
        return -1;
    }
    if (fat_size_sectors == 0) {
        tty_puts("[fat16] not FAT16 (FAT32?)\n");
        return -1;
    }

    fat_start_lba    = reserved_sectors;
    root_dir_sectors = (root_entries * 32 + bytes_per_sector - 1) / bytes_per_sector;
    root_start_lba   = fat_start_lba + num_fats * fat_size_sectors;
    data_start_lba   = root_start_lba + root_dir_sectors;

    tty_puts("[ok] FAT16: spc="); tty_put_hex(sectors_per_cluster);
    tty_puts(" fats="); tty_put_hex(num_fats);
    tty_puts(" root_ent="); tty_put_hex(root_entries);
    tty_puts(" data_lba="); tty_put_hex(data_start_lba);
    tty_putc('\n');

    fat16_ok = 1;
    return 0;
}

/* Получить следующую позицию в FAT-цепочке */
static uint16_t fat_next(uint16_t cluster) {
    uint32_t fat_byte = (uint32_t)cluster * 2;
    uint32_t lba      = fat_start_lba + (fat_byte / 512);
    uint32_t off      = fat_byte % 512;
    uint8_t  buf[512];
    if (rd_sector(lba, buf) != 0) return 0xFFFF;
    return buf[off] | ((uint16_t)buf[off+1] << 8);
}

static uint32_t cluster_to_lba(uint16_t cluster) {
    return data_start_lba + (uint32_t)(cluster - 2) * sectors_per_cluster;
}

void fat16_ls(fat16_ls_cb cb, void* user) {
    if (!fat16_ok) return;
    uint8_t buf[512];
    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        if (rd_sector(root_start_lba + s, buf) != 0) return;
        for (uint32_t e = 0; e < 16; e++) {
            struct fat16_dirent* de = (struct fat16_dirent*)(buf + e * 32);
            if (de->name[0] == 0x00) return;    /* конец директории */
            if (de->name[0] == 0xE5) continue;  /* удалённый */
            if (de->attr == FAT16_ATTR_LFN) continue; /* LFN, пропускаем */
            if (de->attr & FAT16_ATTR_VOLUME) continue;
            if (cb(de, user)) return;
        }
    }
}

/* Сравниваем имя в 8.3 формате с "NAME.EXT" */
static int name_eq(const struct fat16_dirent* de, const char* name) {
    char buf[12];
    int n = 0;
    for (int i = 0; i < 8; i++) {
        if (de->name[i] == ' ') break;
        buf[n++] = de->name[i];
    }
    if (de->ext[0] != ' ') {
        buf[n++] = '.';
        for (int i = 0; i < 3; i++) {
            if (de->ext[i] == ' ') break;
            buf[n++] = de->ext[i];
        }
    }
    buf[n] = 0;

    int i = 0;
    while (buf[i] && name[i]) {
        char a = buf[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        i++;
    }
    return buf[i] == 0 && name[i] == 0;
}

int fat16_find(const char* name, struct fat16_dirent* out) {
    if (!fat16_ok) return -1;
    uint8_t buf[512];
    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        if (rd_sector(root_start_lba + s, buf) != 0) return -1;
        for (uint32_t e = 0; e < 16; e++) {
            struct fat16_dirent* de = (struct fat16_dirent*)(buf + e * 32);
            if (de->name[0] == 0x00) return -1;
            if (de->name[0] == 0xE5) continue;
            if (de->attr == FAT16_ATTR_LFN) continue;
            if (de->attr & FAT16_ATTR_VOLUME) continue;
            if (name_eq(de, name)) {
                for (uint32_t i = 0; i < 32; i++) ((uint8_t*)out)[i] = ((uint8_t*)de)[i];
                return 0;
            }
        }
    }
    return -1;
}

uint32_t fat16_read(const struct fat16_dirent* de, uint8_t* out, uint32_t max) {
    if (!fat16_ok) return 0;
    uint32_t written = 0;
    uint16_t cluster = de->cluster_lo;
    uint32_t remain  = de->size;
    if (remain > max) remain = max;

    uint8_t buf[512];
    while (cluster >= 2 && cluster < 0xFFF8 && remain > 0) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster && remain > 0; s++) {
            if (rd_sector(lba + s, buf) != 0) return written;
            uint32_t chunk = (remain < 512) ? remain : 512;
            for (uint32_t i = 0; i < chunk; i++) out[written + i] = buf[i];
            written += chunk;
            remain  -= chunk;
        }
        cluster = fat_next(cluster);
    }
    return written;
}

void fat16_name(const struct fat16_dirent* de, char* out) {
    int n = 0;
    for (int i = 0; i < 8; i++) {
        if (de->name[i] == ' ') break;
        out[n++] = de->name[i];
    }
    if (de->ext[0] != ' ') {
        out[n++] = '.';
        for (int i = 0; i < 3; i++) {
            if (de->ext[i] == ' ') break;
            out[n++] = de->ext[i];
        }
    }
    out[n] = 0;
}

/* ============ WRITE SUPPORT ============ */

/* Инвалидация кэша после изменения сектора */
static void cache_invalidate(void) {
    sector_buf_valid = 0;
}

static int wr_sector(uint32_t lba, const uint8_t* in) {
    if (ata_write_sector(lba, in) != 0) return -1;
    /* обновить кэш, если это тот же сектор */
    if (sector_buf_valid && sector_buf_lba == lba) {
        for (int i = 0; i < 512; i++) sector_buf[i] = in[i];
    }
    return 0;
}

/* Записать FAT-слово для кластера */
static int fat_set(uint16_t cluster, uint16_t value) {
    uint32_t fat_byte = (uint32_t)cluster * 2;
    uint32_t lba      = fat_start_lba + (fat_byte / 512);
    uint32_t off      = fat_byte % 512;
    uint8_t  buf[512];
    if (ata_read_sector(lba, buf) != 0) return -1;
    buf[off]     = value & 0xFF;
    buf[off + 1] = (value >> 8) & 0xFF;
    if (ata_write_sector(lba, buf) != 0) return -1;
    cache_invalidate();
    return 0;
}

/* Найти свободный кластер (FAT[i] == 0), вернуть его номер или 0 */
static uint16_t alloc_cluster(void) {
    uint8_t buf[512];
    for (uint32_t s = 0; s < fat_size_sectors; s++) {
        if (ata_read_sector(fat_start_lba + s, buf) != 0) return 0;
        for (int i = 0; i < 256; i++) {
            uint16_t v = buf[i*2] | ((uint16_t)buf[i*2+1] << 8);
            if (v == 0) {
                uint32_t cluster = s * 256 + i;
                if (cluster < 2) continue;
                /* помечаем как конец цепочки */
                fat_set(cluster, 0xFFFF);
                return cluster;
            }
        }
    }
    return 0;
}

/* Освободить всю цепочку кластеров */
static void free_chain(uint16_t cluster) {
    while (cluster >= 2 && cluster < 0xFFF8) {
        uint16_t next = fat_next(cluster);
        fat_set(cluster, 0);
        cluster = next;
    }
}

/* Записать dirent обратно в root dir (по индексу сектора/смещения) */
static int root_update(uint32_t sector_idx, uint32_t entry_idx,
                       const struct fat16_dirent* de) {
    uint8_t buf[512];
    uint32_t lba = root_start_lba + sector_idx;
    if (ata_read_sector(lba, buf) != 0) return -1;
    uint8_t* dst = buf + entry_idx * 32;
    for (int i = 0; i < 32; i++) dst[i] = ((const uint8_t*)de)[i];
    if (ata_write_sector(lba, buf) != 0) return -1;
    cache_invalidate();
    return 0;
}

/* Найти свободный слот в root dir и позицию для нового файла */
static int find_free_root_slot(uint32_t* sector_out, uint32_t* entry_out) {
    uint8_t buf[512];
    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        if (ata_read_sector(root_start_lba + s, buf) != 0) return -1;
        for (uint32_t e = 0; e < 16; e++) {
            uint8_t first = buf[e * 32];
            if (first == 0x00 || first == 0xE5) {
                *sector_out = s;
                *entry_out  = e;
                return 0;
            }
        }
    }
    return -1;  /* root dir полон */
}

/* Сформировать 8.3 имя из строки "name.ext" */
static void name_to_83(const char* name, uint8_t* out) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[j++] = c;
    }
    if (name[i] == '.') {
        i++; j = 8;
        while (name[i] && j < 11) {
            char c = name[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[j++] = c;
        }
    }
}

int fat16_create(const char* name) {
    if (!fat16_ok) return -1;
    struct fat16_dirent de;
    if (fat16_find(name, &de) == 0) {
        tty_puts("[fat16] file exists\n");
        return -1;
    }

    uint32_t s_idx, e_idx;
    if (find_free_root_slot(&s_idx, &e_idx) != 0) {
        tty_puts("[fat16] root dir full\n");
        return -1;
    }

    struct fat16_dirent nd;
    uint8_t* p = (uint8_t*)&nd;
    for (int i = 0; i < 32; i++) p[i] = 0;
    name_to_83(name, nd.name);
    nd.attr     = FAT16_ATTR_ARCH;
    nd.cluster_lo = 0;   /* пока пусто */
    nd.size       = 0;

    if (root_update(s_idx, e_idx, &nd) != 0) return -1;
    return 0;
}

uint32_t fat16_write(const char* name, const uint8_t* buf, uint32_t size, uint32_t offset) {
    if (!fat16_ok) return 0;
    struct fat16_dirent de;
    if (fat16_find(name, &de) != 0) return 0;

    /* Найдём сектор и entry для этого dirent — нужно для обновления size */
    uint32_t dir_sector = 0, dir_entry = 0;
    {
        uint8_t b[512];
        int found = 0;
        for (uint32_t s = 0; s < root_dir_sectors && !found; s++) {
            if (ata_read_sector(root_start_lba + s, b) != 0) return 0;
            for (uint32_t e = 0; e < 16; e++) {
                struct fat16_dirent* d2 = (struct fat16_dirent*)(b + e * 32);
                if (d2->name[0] == 0x00 || d2->name[0] == 0xE5) continue;
                if (d2->attr == FAT16_ATTR_LFN) continue;
                /* сравним 8.3 имя */
                int same = 1;
                for (int k = 0; k < 11; k++) {
                    if (((uint8_t*)d2)[k] != ((uint8_t*)&de)[k]) { same = 0; break; }
                }
                if (same) { dir_sector = s; dir_entry = e; found = 1; break; }
            }
        }
        if (!found) return 0;
    }

    /* Если в dirent не было кластера — выделяем первый */
    uint16_t first = de.cluster_lo;
    if (first < 2) {
        first = alloc_cluster();
        if (first == 0) return 0;
        de.cluster_lo = first;
    }

    /* Дойти до кластера, содержащего offset, создавая цепочки по пути */
    uint32_t cluster_bytes = sectors_per_cluster * 512;
    uint32_t skip_clusters = offset / cluster_bytes;
    uint32_t skip_bytes    = offset % cluster_bytes;

    uint16_t cluster = first;
    for (uint32_t k = 0; k < skip_clusters; k++) {
        uint16_t next = fat_next(cluster);
        if (next >= 0xFFF8) {
            next = alloc_cluster();
            if (next == 0) return 0;
            fat_set(cluster, next);
        }
        cluster = next;
    }

    /* Пишем по секторам, при переходе через кластер — следующая цепочка */
    uint32_t written = 0;
    uint32_t in_cluster_off = skip_bytes;
    uint8_t  sbuf[512];

    while (written < size) {
        /* Определяем сектор внутри текущего кластера */
        uint32_t sector_in_cluster = in_cluster_off / 512;
        uint32_t byte_in_sector    = in_cluster_off % 512;

        uint32_t lba = cluster_to_lba(cluster) + sector_in_cluster;
        if (ata_read_sector(lba, sbuf) != 0) return written;

        uint32_t chunk = 512 - byte_in_sector;
        if (chunk > size - written) chunk = size - written;

        for (uint32_t i = 0; i < chunk; i++)
            sbuf[byte_in_sector + i] = buf[written + i];

        if (ata_write_sector(lba, sbuf) != 0) return written;
        cache_invalidate();

        written += chunk;
        in_cluster_off += chunk;

        /* Если перешли в следующий кластер */
        if (in_cluster_off >= cluster_bytes) {
            in_cluster_off = 0;
            uint16_t next = fat_next(cluster);
            if (next >= 0xFFF8) {
                next = alloc_cluster();
                if (next == 0) break;
                fat_set(cluster, next);
            }
            cluster = next;
        }
    }

    /* Обновить size в dirent */
    uint32_t new_size = offset + written;
    if (new_size > de.size) de.size = new_size;
    root_update(dir_sector, dir_entry, &de);

    return written;
}

int fat16_delete(const char* name) {
    if (!fat16_ok) return -1;
    uint8_t b[512];
    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        if (ata_read_sector(root_start_lba + s, b) != 0) return -1;
        for (uint32_t e = 0; e < 16; e++) {
            struct fat16_dirent* d = (struct fat16_dirent*)(b + e * 32);
            if (d->name[0] == 0x00) return -1;
            if (d->name[0] == 0xE5) continue;
            if (d->attr == FAT16_ATTR_LFN) continue;
            if (d->attr & FAT16_ATTR_VOLUME) continue;
            if (name_eq(d, name)) {
                if (d->cluster_lo >= 2)
                    free_chain(d->cluster_lo);
                b[e * 32] = 0xE5;
                if (ata_write_sector(root_start_lba + s, b) != 0) return -1;
                cache_invalidate();
                return 0;
            }
        }
    }
    return -1;
}
