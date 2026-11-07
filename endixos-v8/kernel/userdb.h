#ifndef ENDIX_USERDB_H
#define ENDIX_USERDB_H
#include "types.h"
typedef struct { char user[16], full[48], host[32], salthash[100]; } userdb_t;
/* Ищет на SATA-дисках систему, поставленную установщиком (/system/users.dat на первом разделе FAT32).
 * 0 - найдено и разобрано, иначе -1. */
int userdb_load(userdb_t *u);
#endif
