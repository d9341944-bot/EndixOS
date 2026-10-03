#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>

void     mouse_init(void);
void     mouse_poll(void);
void     mouse_toggle_verbose(void);

int      mouse_x(void);
int      mouse_y(void);
int      mouse_btn_left(void);
int      mouse_btn_right(void);
int      mouse_btn_middle(void);

#endif
