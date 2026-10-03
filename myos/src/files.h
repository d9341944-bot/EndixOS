#ifndef FILES_H
#define FILES_H
#include <stdint.h>

/* Файловый менеджер как окно wm */
void files_open(void);      /* создать окно File Manager */
void files_handle_key(int c);
void files_init(void);

#endif
