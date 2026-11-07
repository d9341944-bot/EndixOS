#ifndef ENDIX_WM_H
#define ENDIX_WM_H

#include "../kernel/types.h"
#include "../kernel/event.h"

typedef struct {
    char cpu[64];
    u32  ram_mb;
    u32  width, height;
} sysinfo_t;

/* Рисует обои, открывает стартовые окна. Вызывать после fb_init. */
void wm_init(const sysinfo_t *info);

/* Обработка одного события ввода (мышь или клавиатура). */
void wm_handle_event(const event_t *e);

/* Текущее время (из RTC). Возвращает 1, если изменилась видимая часть часов. */
int  wm_set_time(u32 hour, u32 min, u32 day, u32 month, u32 year);
void wm_set_uptime(u32 seconds);

/* Вызывать из главного цикла на каждом проходе: игры и анимации. 1 - нужна перерисовка. */
int  wm_tick(u32 ticks);
u32  wm_get_ticks(void);             /* текущие тики PIT */

/* Показать кадр, если что-то изменилось (ввод сам помечает сцену изменённой).
 * 1 - кадр выведен. wm_invalidate() - сцена изменилась не из-за ввода (часы, игра). */
int  wm_frame(u32 ticks);
void wm_invalidate(void);

/* Настройки оболочки (Settings > Appearance). */
enum { WM_OPT_DOCK_MAGNIFY, WM_OPT_CLOCK24, WM_OPT_REDUCE_TRANSP, WM_OPT_DOCK_ZOOM, WM_OPT_DOCK_HIDE };
int  wm_get_option(int id);
void wm_set_option(int id, int v);

/* Перерисовать весь кадр в теневой буфер (потом fb_flip). */
void wm_render(void);

/* Реализует ядро: перезагрузка машины. */
void sys_reboot(void);
void wm_live_hint(void);                                 /* показать подсказку про установщик */
void wm_set_user(const char *user, const char *host);   /* имя из установленной системы: whoami, hostname */
void sys_shutdown(void);

/* Диагностика сети для ifconfig: счётчики кадров и состояние связи. */
void wm_set_net_stats(void (*f)(u32 *rx, u32 *tx, u32 *txfail, int *link));

#endif
