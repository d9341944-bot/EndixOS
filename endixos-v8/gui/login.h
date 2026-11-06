#ifndef ENDIX_LOGIN_H
#define ENDIX_LOGIN_H
#include "../kernel/types.h"

/* Экран входа в стиле macOS: размытые обои, часы сверху, пользователь и пароль снизу. */
void login_open(const char *user, const char *fullname, const char *salthash);   /* salthash: "соль32hex:хэш64hex" */
int  login_relock(void);                         /* показать экран входа снова (команда lock); 0, если пользователя нет */
int  login_active(void);                         /* виден (в том числе пока гаснет) */
int  login_fading(void);                         /* пароль принят, экран плавно гаснет */
i32  login_alpha(void);                          /* 1024 - экран входа виден полностью, 0 - исчез */
void login_draw(int have_time, u32 hour, u32 min, u32 day, u32 month, u32 year, int clock24);
int  login_mouse(i32 x, i32 y, int btn);         /* 1 - нужна перерисовка */
int  login_key(int c);
int  login_tick(u32 ticks);
const char *login_user(void);
#endif
