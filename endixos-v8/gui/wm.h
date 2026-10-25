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

/* Показать кадр, если что-то изменилось (ввод сам помечает сцену изменённой).
 * 1 - кадр выведен. wm_invalidate() - сцена изменилась не из-за ввода (часы, игра). */
int  wm_frame(u32 ticks);
void wm_invalidate(void);

/* Настройки оболочки (Settings > Appearance). */
enum { WM_OPT_DOCK_MAGNIFY, WM_OPT_CLOCK24 };
int  wm_get_option(int id);
void wm_set_option(int id, int v);

/* Перерисовать весь кадр в теневой буфер (потом fb_flip). */
void wm_render(void);

/* Реализует ядро: перезагрузка машины. */
void sys_reboot(void);
void sys_shutdown(void);

#endif
