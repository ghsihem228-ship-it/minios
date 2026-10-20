/* Pure wire validation shared by the kernel and host sanitizer tests. */
#include "tcp_wire.h"
#include "net_wire.h"
uint32_t tcp_pseudo(uint32_t source, uint32_t destination, unsigned length) {
    return (source >> 16) + (source & 65535) + (destination >> 16) + (destination & 65535) + 6 +
           length;
}
int tcp_decode(uint32_t source, uint32_t destination, const uint8_t *p, unsigned len,
               struct tcp_packet *out) {
    if (len < 20 || len > 1480)
        return -22;
    unsigned header = (p[12] >> 4) * 4;
    if (header < 20 || header > len || !nw16(p) || !nw16(p + 2) || (p[12] & 15) || (p[13] & 0xC0) ||
        nw_checksum(p, len, tcp_pseudo(source, destination, len)))
        return -22;
    *out = (struct tcp_packet){.source = nw16(p),
                               .destination = nw16(p + 2),
                               .seq = nw32(p + 4),
                               .ack = nw32(p + 8),
                               .window = nw16(p + 14),
                               .flags = p[13],
                               .data = p + header,
                               .length = len - header,
                               .mss = 536};
    /* No urgent data, ECN or contradictory control flags. */
    if ((out->flags & 32) || ((out->flags & TCP_SYN) && (out->flags & (TCP_FIN | TCP_RST))) ||
        ((out->flags & TCP_RST) && (out->flags & TCP_FIN)))
        return -22;
    unsigned pos = 20;
    while (pos < header) {
        unsigned kind = p[pos++];
        if (!kind)
            break;
        if (kind == 1)
            continue;
        if (pos >= header)
            return -22;
        unsigned size = p[pos++];
        if (size < 2 || size - 2 > header - pos)
            return -22;
        if (kind == 2) {
            if (size != 4 || !(out->flags & TCP_SYN) || !nw16(p + pos))
                return -22;
            out->mss = nw16(p + pos);
        }
        pos += size - 2;
    }
    return 0;
}
