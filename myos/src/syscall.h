#ifndef SYSCALL_H
#define SYSCALL_H
#include "isr.h"
void syscall_handler(struct regs* r);
#endif
