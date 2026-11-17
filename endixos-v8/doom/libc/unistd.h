#ifndef L_UNISTD_H
#define L_UNISTD_H
#include <sys/types.h>
int access(const char *p, int m);
#define R_OK 4
#define W_OK 2
#define F_OK 0
#endif
