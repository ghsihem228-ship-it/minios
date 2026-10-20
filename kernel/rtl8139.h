#ifndef RTL8139_H
#define RTL8139_H
#include <stdint.h>
#define ETH_FRAME_MAX 1514u
int rtl_init(uint8_t mac[6]);
int rtl_link(void);
int rtl_send(const uint8_t *frame, unsigned length);
int rtl_receive(uint8_t *frame, unsigned capacity);
uint32_t rtl_errors(void);
#endif
