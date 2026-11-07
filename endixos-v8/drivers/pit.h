#ifndef ENDIX_PIT_H
#define ENDIX_PIT_H

#include "../kernel/types.h"

#define PIT_HZ 100

void pit_init(void);
u32  pit_ticks(void);          /* тиков с момента запуска, PIT_HZ в секунду */

#endif
