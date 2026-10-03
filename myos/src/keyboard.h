#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>

#define KEY_BUF_SIZE 256
#define KEY_UP     0x101
#define KEY_DOWN   0x102
#define KEY_LEFT   0x103
#define KEY_RIGHT  0x104
#define KEY_DELETE 0x105
#define KEY_HOME   0x106
#define KEY_END    0x107
#define KEY_F1     0x110
#define KEY_F2     0x111
#define KEY_F3     0x112
#define KEY_F4     0x113
#define KEY_F5     0x114
#define KEY_F6     0x115
#define KEY_F7     0x116
#define KEY_F8     0x117
#define KEY_F9     0x118
#define KEY_F10    0x119
#define KEY_F11    0x11A
#define KEY_F12    0x11B

void keyboard_init(void);
int  keyboard_has_char(void);
int  keyboard_getchar(void);
#endif
