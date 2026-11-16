#ifndef ENDIX_ACPI_H
#define ENDIX_ACPI_H
#include "types.h"
int acpi_poweroff(void);                                           /* возвращается только при неудаче */
int acpi_parse_s5(const u8 *dsdt, u32 len, u8 *a, u8 *b);          /* SLP_TYPa/b из _S5_; 0 - найдено */
const char *acpi_diag(void);                                        /* что нашли и на каком шаге остановились */
#endif
