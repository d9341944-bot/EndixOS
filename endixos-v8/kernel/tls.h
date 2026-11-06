#ifndef ENDIX_TLS_H
#define ENDIX_TLS_H

#include "types.h"

/* TLS 1.2 клиент: ECDHE (X25519) + AES-128-GCM. Зашифрованное соединение, но сертификат сервера НЕ проверяется
 * (нужен список корневых центров и RSA/ECDSA: это следующий этап). Кадры TCP приходят в tls_input, ответы уходят через cb.send. */

typedef struct {
    void (*send)(const u8 *d, u32 n);            /* байты для отправки по TCP (за один шаг может быть несколько вызовов) */
    void (*ready)(void);                         /* рукопожатие закончено, можно tls_write */
    void (*data)(const u8 *d, u32 n);            /* расшифрованные данные */
    void (*closed)(const char *err);             /* err == 0: сервер закрыл соединение штатно */
} tls_cb_t;

void tls_start(const tls_cb_t *cb, const char *sni);
void tls_input(const u8 *d, u32 n);
void tls_write(const u8 *d, u32 n);              /* до ~1100 байт за раз */
int  tls_active(void);
void tls_reset(void);

#endif
