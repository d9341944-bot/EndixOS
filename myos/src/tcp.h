#ifndef TCP_H
#define TCP_H
#include <stdint.h>

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;      /* 4 бита offset, 4 зарезервировано */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10

void tcp_init(void);
int  tcp_connect(uint32_t ip, uint16_t port);
int  tcp_send(const void* data, uint32_t len);
int  tcp_recv(void* buf, uint32_t max);      /* -1 нет данных, 0 закрыто, >0 прочитано */
int  tcp_state(void);
void tcp_close(void);
void tcp_handle(uint32_t src_ip, const uint8_t* data, uint32_t len);

#endif
