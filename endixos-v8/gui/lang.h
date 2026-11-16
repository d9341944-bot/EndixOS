#ifndef ENDIX_LANG_H
#define ENDIX_LANG_H
/* Язык интерфейса оболочки: 0 = English, 1 = Русский. tr() переводит английскую строку; неизвестные остаются как есть. */
int         lang_get(void);
void        lang_set(int l);
const char *tr(const char *en);
#endif
