/* Run the same packet decoders used by the kernel under host sanitizers. */
#include "net_wire.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint32_t seed = 12345;
static unsigned random32(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
int main(void) {
    uint8_t packet[600] = {0}, mac[6] = {0};
    uint32_t ip, ttl;
    struct dhcp_offer offer;
    assert(nw_ip("10.0.2.15", &ip) && ip == 0x0a00020f);
    const char *bad[] = {"", "1.2.3", "1.2.3.256", "1.2.3.4x", "1..3.4", "1234.0.0.1"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++)
        assert(!nw_ip(bad[i], &ip));
    int n = nw_dns_query(packet, sizeof(packet), "hello.test", 42);
    assert(n > 0);
    nw_put16(packet + 2, 0x8180);
    nw_put16(packet + 6, 1);
    packet[n++] = 0xc0;
    packet[n++] = 12;
    nw_put16(packet + n, 1);
    nw_put16(packet + n + 2, 1);
    nw_put32(packet + n + 4, 60);
    nw_put16(packet + n + 8, 4);
    nw_put32(packet + n + 10, 0x0a000202);
    n += 14;
    assert(!nw_dns_answer(packet, n, "hello.test", 42, &ip, &ttl) && ip == 0x0a000202 && ttl == 60);
    for (int k = 0; k < n; k++)
        assert(nw_dns_answer(packet, k, "hello.test", 42, &ip, &ttl) < 0);
    packet[12] = 0xc0;
    packet[13] = 12;
    assert(nw_dns_answer(packet, n, "hello.test", 42, &ip, &ttl) == -22);
    memset(packet, 0, sizeof(packet));
    packet[0] = 2;
    packet[1] = 1;
    packet[2] = 6;
    nw_put32(packet + 4, 42);
    nw_put32(packet + 236, 0x63825363);
    uint8_t options[] = {53, 1, 2, 54, 4, 10, 0, 2, 2, 255};
    memcpy(packet + 240, options, sizeof(options));
    assert(!nw_dhcp(packet, 250, 42, mac, &offer) && offer.type == 2);
    for (unsigned k = 0; k < 250; k++)
        assert(nw_dhcp(packet, k, 42, mac, &offer) < 0);
    for (unsigned trial = 0; trial < 100000; trial++) {
        unsigned length = random32() % sizeof(packet);
        for (unsigned i = 0; i < length; i++)
            packet[i] = random32();
        if (trial & 1) { /* Reach beyond header validation into DNS/DHCP structures. */
            if (length >= 12) {
                nw_put16(packet, 42);
                nw_put16(packet + 2, 0x8180);
                nw_put16(packet + 4, 1);
            }
            if (length >= 240) {
                packet[0] = 2;
                packet[1] = 1;
                packet[2] = 6;
                nw_put32(packet + 4, 42);
                memset(packet + 28, 0, 6);
                nw_put32(packet + 236, 0x63825363);
            }
        }
        nw_dns_answer(packet, length, "hello.test", 42, &ip, &ttl);
        nw_dhcp(packet, length, 42, mac, &offer);
        nw_checksum(packet, length, 0);
    }
    puts("PASS wire: strict IPv4, DNS compression/truncation, DHCP TLVs, 100000 fuzz cases");
}
