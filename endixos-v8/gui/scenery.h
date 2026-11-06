#ifndef ENDIX_SCENERY_H
#define ENDIX_SCENERY_H
#include "../kernel/types.h"

/* Встроенные 3D-обои (заранее отрендеренные JPEG, упакованы в ядро как assets/wall.pak). */
int  scenery_count(void);
const char *scenery_name(int k);
int  scenery_paint(int k, u32 *dst, u32 w, u32 h);   /* 1 - успех */
#endif
