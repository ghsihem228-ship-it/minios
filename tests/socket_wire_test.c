#include "tcp_wire.h"
#include "net_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t seed = 7;
static uint32_t random32(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
static void seal(uint8_t *p, unsigned n) {
    nw_put16(p + 16, 0);
    nw_put16(p + 16, nw_checksum(p, n, tcp_pseudo(0x0a000202, 0x0a00020f, n)));
}
int main(void) {
    uint8_t p[1500] = {0};
    struct tcp_packet v;
    nw_put16(p, 8080);
    nw_put16(p + 2, 32768);
    nw_put32(p + 4, 0xfffffff0);
    nw_put32(p + 8, 13);
    p[12] = 0x60;
    p[13] = TCP_SYN | TCP_ACK;
    nw_put16(p + 14, 2048);
    p[20] = 2;
    p[21] = 4;
    nw_put16(p + 22, 512);
    seal(p, 24);
    assert(!tcp_decode(0x0a000202, 0x0a00020f, p, 24, &v) && v.mss == 512 && v.seq == 0xfffffff0);
    for (unsigned n = 0; n < 24; n++)
        assert(tcp_decode(0x0a000202, 0x0a00020f, p, n, &v) < 0);
    p[21] = 255;
    seal(p, 24);
    assert(tcp_decode(0x0a000202, 0x0a00020f, p, 24, &v) < 0);
    p[21] = 4;
    p[13] = TCP_SYN | TCP_FIN;
    seal(p, 24);
    assert(tcp_decode(0x0a000202, 0x0a00020f, p, 24, &v) < 0);
    for (unsigned k = 0; k < 100000; k++) {
        unsigned n = random32() % sizeof(p);
        for (unsigned i = 0; i < n; i++)
            p[i] = random32();
        if (n >= 20 && (k & 1))
            seal(p, n);
        tcp_decode(0x0a000202, 0x0a00020f, p, n, &v);
    }
    puts("PASS TCP wire: checksum, header/options/control flags and 100000 sanitizer fuzz cases");
}
