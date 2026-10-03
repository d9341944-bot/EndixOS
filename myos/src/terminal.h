#ifndef TERMINAL_H
#define TERMINAL_H
#include <stdint.h>

void terminal_open(void);
void terminal_handle_key(int c);
int  terminal_win_id(void);
int  terminal_is_open(void);

#endif
