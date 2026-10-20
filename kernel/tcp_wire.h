#ifndef TCP_WIRE_H
#define TCP_WIRE_H
#include <stdint.h>
#define TCP_FIN 1u
#define TCP_SYN 2u
#define TCP_RST 4u
#define TCP_PSH 8u
#define TCP_ACK 16u
struct tcp_packet {
    uint32_t seq, ack;
    uint16_t source, destination, window, mss;
    uint8_t flags;
    const uint8_t *data;
    unsigned length;
};
uint32_t tcp_pseudo(uint32_t source, uint32_t destination, unsigned length);
int tcp_decode(uint32_t source, uint32_t destination, const uint8_t *p, unsigned length,
               struct tcp_packet *out);
#endif
