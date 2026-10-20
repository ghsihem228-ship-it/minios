/* Pure bounded wire codecs; also compiled under host sanitizers. */
#include "net_wire.h"
uint16_t nw16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
uint32_t nw32(const uint8_t *p) { return ((uint32_t)nw16(p) << 16) | nw16(p + 2); }
void nw_put16(uint8_t *p, uint16_t n) {
    p[0] = n >> 8;
    p[1] = n;
}
void nw_put32(uint8_t *p, uint32_t n) {
    nw_put16(p, n >> 16);
    nw_put16(p + 2, n);
}
int nw_equal(const void *a, const void *b, unsigned len) {
    const uint8_t *x = a, *y = b;
    for (unsigned i = 0; i < len; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}
uint16_t nw_checksum(const uint8_t *p, unsigned len, uint32_t sum) {
    while (len > 1) {
        sum += nw16(p);
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint16_t)*p << 8;
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
uint32_t nw_pseudo(uint32_t src, uint32_t dst, unsigned len) {
    return (src >> 16) + (src & 65535) + (dst >> 16) + (dst & 65535) + 17 + len;
}
int nw_unicast(uint32_t ip) { return (ip >> 24) > 0 && (ip >> 24) < 224 && (ip >> 24) != 127; }
int nw_ip(const char *p, uint32_t *out) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned octet = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            octet = octet * 10 + (*p++ - '0');
            if (++digits > 3 || octet > 255)
                return 0;
        }
        if (!digits || (i < 3 && *p++ != '.') || (i == 3 && *p))
            return 0;
        value = (value << 8) | octet;
    }
    *out = value;
    return 1;
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int names_equal(const char *a, const char *b) {
    while (*a && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return *a == *b;
}
int nw_dns_query(uint8_t *out, unsigned cap, const char *name, uint16_t id) {
    if (cap < 18)
        return -22;
    for (unsigned i = 0; i < 12; i++)
        out[i] = 0;
    nw_put16(out, id);
    nw_put16(out + 2, 0x0100);
    nw_put16(out + 4, 1);
    unsigned pos = 12, total = 0;
    while (*name) {
        unsigned at = pos++, count = 0;
        if (pos >= cap)
            return -22;
        while (*name && *name != '.') {
            char c = *name++;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '-'))
                return -22;
            if (++count > 63 || ++total > 253 || pos >= cap)
                return -22;
            out[pos++] = c;
        }
        if (!count)
            return -22;
        out[at] = count;
        if (*name == '.') {
            name++;
            total++;
            if (!*name)
                return -22;
        }
    }
    if (pos == 12 || pos + 5 > cap)
        return -22;
    out[pos++] = 0;
    nw_put16(out + pos, 1);
    nw_put16(out + pos + 2, 1);
    return pos + 4;
}
/* Consumed length is distinct from followed compression offsets. */
static int dns_name(const uint8_t *p, unsigned len, unsigned at, char out[254],
                    unsigned *consumed) {
    unsigned used = 0, written = 0;
    int jumped = 0;
    for (unsigned hops = 0; hops < 128; hops++) {
        if (at >= len)
            return -22;
        unsigned n = p[at++];
        if (!jumped)
            used++;
        if ((n & 0xC0) == 0xC0) {
            if (at >= len)
                return -22;
            unsigned target = ((n & 63) << 8) | p[at];
            if (target >= len)
                return -22;
            if (!jumped)
                used++;
            jumped = 1;
            at = target;
            continue;
        }
        if (n & 0xC0)
            return -22;
        if (!n) {
            out[written] = 0;
            *consumed = used;
            return 0;
        }
        if (n > len - at || written + n + (written ? 1 : 0) > 253)
            return -22;
        if (written)
            out[written++] = '.';
        for (unsigned i = 0; i < n; i++) {
            unsigned char c = p[at + i];
            if (c < 33 || c > 126 || c == '.')
                return -22;
            out[written++] = c;
        }
        at += n;
        if (!jumped)
            used += n;
    }
    return -22;
}
int nw_dns_answer(const uint8_t *p, unsigned len, const char *name, uint16_t id, uint32_t *ip,
                  uint32_t *ttl) {
    if (len < 12 || len > 512 || nw16(p) != id || (nw16(p + 2) & 0xF800) != 0x8000 ||
        nw16(p + 4) != 1)
        return -22;
    char question[254];
    unsigned used;
    if (dns_name(p, len, 12, question, &used) || !names_equal(question, name))
        return -22;
    unsigned start = 12 + used;
    if (start > len || len - start < 4 || nw16(p + start) != 1 || nw16(p + start + 2) != 1)
        return -22;
    if (nw16(p + 2) & 0x0200)
        return -75;
    unsigned rcode = nw16(p + 2) & 15;
    if (rcode)
        return rcode == 3 ? -2 : -5;
    unsigned answers = nw16(p + 6), records = answers + nw16(p + 8) + nw16(p + 10);
    if (answers > 16 || records > 32)
        return -22;
    start += 4;
    char expected[254];
    unsigned n = 0;
    while (name[n] && n < 253) {
        expected[n] = name[n];
        n++;
    }
    expected[n] = 0;
    uint32_t chain_ttl = UINT32_MAX;
    for (unsigned hop = 0; hop < 5; hop++) {
        unsigned pos = start;
        char alias[254] = {0};
        int found = 0;
        uint32_t candidate = 0, lifetime = 0;
        for (unsigned rr = 0; rr < records; rr++) {
            char owner[254];
            if (dns_name(p, len, pos, owner, &used))
                return -22;
            pos += used;
            if (pos > len || len - pos < 10)
                return -22;
            unsigned type = nw16(p + pos), cls = nw16(p + pos + 2), size = nw16(p + pos + 8);
            uint32_t life = nw32(p + pos + 4);
            pos += 10;
            if (size > len - pos)
                return -22;
            if (rr < answers && cls == 1 && names_equal(owner, expected)) {
                if (type == 1 && size == 4 && nw_unicast(nw32(p + pos))) {
                    candidate = nw32(p + pos);
                    lifetime = life;
                    found = 1;
                } else if (type == 5) {
                    if (dns_name(p, len, pos, alias, &used) || used != size)
                        return -22;
                    if (life < chain_ttl)
                        chain_ttl = life;
                }
            }
            pos += size;
        }
        if (found) {
            *ip = candidate;
            *ttl = lifetime < chain_ttl ? lifetime : chain_ttl;
            return 0;
        }
        if (!alias[0])
            return -2;
        for (n = 0; alias[n]; n++)
            expected[n] = alias[n];
        expected[n] = 0;
    }
    return -22;
}
int nw_dhcp(const uint8_t *p, unsigned len, uint32_t xid, const uint8_t mac[6],
            struct dhcp_offer *o) {
    if (len < 241 || p[0] != 2 || p[1] != 1 || p[2] != 6 || nw32(p + 4) != xid ||
        !nw_equal(p + 28, mac, 6) || nw32(p + 236) != 0x63825363)
        return -22;
    *o = (struct dhcp_offer){.ip = nw32(p + 16)};
    unsigned pos = 240, seen = 0;
    int end = 0;
    while (pos < len) {
        unsigned code = p[pos++];
        if (code == 255) {
            end = 1;
            break;
        }
        if (!code)
            continue;
        if (pos >= len)
            return -22;
        unsigned size = p[pos++];
        if (size > len - pos)
            return -22;
        unsigned bit = code == 53   ? 1
                       : code == 54 ? 2
                       : code == 1  ? 4
                       : code == 3  ? 8
                       : code == 6  ? 16
                       : code == 51 ? 32
                                    : 0;
        if (bit && (seen & bit))
            return -22;
        seen |= bit;
        if (code == 53) {
            if (size != 1)
                return -22;
            o->type = p[pos];
        } else if (code == 54 || code == 1 || code == 51) {
            if (size != 4)
                return -22;
            uint32_t val = nw32(p + pos);
            if (code == 54)
                o->server = val;
            else if (code == 1)
                o->mask = val;
            else
                o->lease = val;
        } else if (code == 3 || code == 6) {
            if (!size || size % 4)
                return -22;
            if (code == 3)
                o->router = nw32(p + pos);
            else
                o->dns = nw32(p + pos);
        } else if (code == 52)
            return -95; /* option overload unsupported */
        pos += size;
    }
    return end && (seen & 3) == 3 ? 0 : -22;
}
