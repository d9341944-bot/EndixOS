#ifndef ENDIX_SCENERY_H
#define ENDIX_SCENERY_H
#include "../kernel/types.h"

/* Встроенные 3D-обои (заранее отрендеренные JPEG, упакованы в ядро как assets/wall.pak). */
int  scenery_count(void);
const char *scenery_name(int k);
int  scenery_dynamic(int k);                          /* пара день/ночь */
int  scenery_hidden(int k);                           /* ночной вариант: в списке не показывается */
int  scenery_night(void);
int  scenery_set_hour(int h);                         /* 1, если день/ночь сменились */
const char *scenery_label(int k);                     /* имя для списка ("Layers (Dynamic)") */
int  scenery_paint(int k, u32 *dst, u32 w, u32 h);   /* 1 - успех */
#endif
