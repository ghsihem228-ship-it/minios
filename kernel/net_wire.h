#ifndef NET_WIRE_H
#define NET_WIRE_H
#include <stdint.h>
uint16_t nw16(const uint8_t *p);
uint32_t nw32(const uint8_t *p);
void nw_put16(uint8_t *p, uint16_t v);
void nw_put32(uint8_t *p, uint32_t v);
uint16_t nw_checksum(const uint8_t *p, unsigned len, uint32_t sum);
uint32_t nw_pseudo(uint32_t src, uint32_t dst, unsigned len);
int nw_equal(const void *a, const void *b, unsigned len);
int nw_ip(const char *text, uint32_t *ip);
int nw_unicast(uint32_t ip);
int nw_dns_query(uint8_t *out, unsigned cap, const char *name, uint16_t id);
int nw_dns_answer(const uint8_t *p, unsigned len, const char *name, uint16_t id, uint32_t *ip,
                  uint32_t *ttl);
struct dhcp_offer {
    uint32_t ip, mask, router, dns, server, lease;
    unsigned type;
};
int nw_dhcp(const uint8_t *p, unsigned len, uint32_t xid, const uint8_t mac[6],
            struct dhcp_offer *out);
#endif
