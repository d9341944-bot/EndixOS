#include "userdb.h"
#include "blk.h"
#include "../drivers/ahci.h"

static int d_rd(void *c, u32 lba, u32 n, void *b) { return ahci_read((int)(long)c, lba, n, b); }
static int d_wr(void *c, u32 lba, u32 n, const void *b) { (void)c; (void)lba; (void)n; (void)b; return -1; }   /* при входе диск только читаем */

static void cp(char *d, const char *s, int n) { int i = 0; while (i < n - 1 && s[i] && s[i] != '\n' && s[i] != '\r') { d[i] = s[i]; i++; } d[i] = 0; }

int userdb_load(userdb_t *u)
{
    static fat_t f; static blk_t b; static char dat[512], hst[64];
    int n = ahci_init();
    for (int i = 0; i < n; i++) {
        const ahci_disk_t *d = ahci_disk(i);
        if (!d) continue;
        b.rd = d_rd; b.wr = d_wr; b.ctx = (void *)(long)i; b.sectors = d->sectors;
        u8 type; u32 start, size;
        if (mbr_get_part(&b, 0, &type, &start, &size) != 0 || (type != 0x0C && type != 0x0B)) continue;
        if (fat_mount(&b, start, &f) != 0) continue;
        int len = fat_read(&f, "/system/users.dat", dat, sizeof dat - 1);
        if (len <= 0) continue;
        dat[len] = 0;
        /* имя:соль32:хэш64:полное имя */
        const char *p = dat; char *fld[4]; int k = 0; fld[0] = 0;
        char *q = dat; char *st = dat;
        for (; *q && *q != '\n' && k < 3; q++)
            if (*q == ':') { *q = 0; fld[k++] = st; st = q + 1; }
        if (k != 3) continue;
        fld[3] = st; (void)p;
        int ls = 0, lh = 0; while (fld[1][ls]) ls++; while (fld[2][lh]) lh++;
        if (ls != 32 || lh != 64) continue;
        cp(u->user, fld[0], 16); cp(u->full, fld[3], 48);
        for (int j = 0; j < 32; j++) u->salthash[j] = fld[1][j];
        u->salthash[32] = ':';
        for (int j = 0; j < 64; j++) u->salthash[33 + j] = fld[2][j];
        u->salthash[97] = 0;
        u->host[0] = 0;
        int hl = fat_read(&f, "/system/host.txt", hst, sizeof hst - 1);
        if (hl > 0) { hst[hl] = 0; cp(u->host, hst, 32); }
        return 0;
    }
    return -1;
}
