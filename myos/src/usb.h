#ifndef USB_H
#define USB_H
#include <stdint.h>

int  usb_tablet_init(void);
void usb_tablet_poll(void);

int  usb_tablet_ready(void);
int  usb_tablet_x(void);   /* 0..1023 */
int  usb_tablet_y(void);   /* 0..767 */
int  usb_tablet_left(void);
int  usb_tablet_right(void);
int  usb_tablet_middle(void);

#endif
