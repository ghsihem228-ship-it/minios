/* IPv4 host stack: bounded shell clients and task-0 UDP/TCP socket service.
 * No forwarding, fragment reassembly or IRQ-side protocol processing. */
#include "net.h"
#include "socket.h"
#include "net_wire.h"
#include "pci.h"
#include "rtl8139.h"
#include "timer.h"
#include "task.h"
#include "keyboard.h"
#include "vga.h"
#include "vfs.h"
#include "heap.h"
#include <stddef.h>

#define BROADCAST 0xFFFFFFFFu
#define DOWNLOAD_MAX (256u * 1024u)
static const uint8_t broadcast[6] = {255, 255, 255, 255, 255, 255};
static uint8_t mac[6], rx_frame[ETH_FRAME_MAX], tx_frame[ETH_FRAME_MAX], udp_tx[1480], icmp[1480];
static int available, configured, forever;
static uint32_t ip, mask, router, dns, lease_end, rx_count, tx_count, drops;
static uint16_t ip_id, port_counter = 49152, transaction;
struct neighbor {
    uint32_t ip, stamp;
    uint8_t mac[6];
    int valid;
};
static struct neighbor neighbors[8];
static uint32_t arp_pending, arp_sent;
static struct dhcp_offer offer;
static uint32_t dhcp_xid;
static int dhcp_phase;
static struct {
    int active, result;
    uint16_t port, id;
    char name[254];
    uint32_t ip, ttl;
} query;
static struct {
    char name[254];
    uint32_t ip, expires;
    int valid;
} dns_cache[4];
static unsigned cache_slot;
static struct {
    int active, ready, duplicate, error;
    uint16_t port, peer, expected, size;
    uint32_t server;
    uint8_t data[512];
} transfer;
static struct {
    int active, received;
    uint32_t peer, sent;
    uint16_t sequence;
} echo;

static int expired(uint32_t deadline) { return (int32_t)(timer_ticks() - deadline) >= 0; }
static int idle(void) {
    net_poll();
    if (keyboard_poll() == 27)
        return -4;
    task_idle();
    return 0;
}
static void print_ip(uint32_t n) {
    for (unsigned i = 0; i < 4; i++) {
        if (i)
            vga_putc('.');
        vga_put_dec((n >> (24 - 8 * i)) & 255);
    }
}
static int emit(const uint8_t *p, unsigned n) {
    int error = rtl_send(p, n);
    if (!error)
        tx_count++;
    return error;
}
static void ethernet(uint16_t type, const uint8_t target[6]) {
    for (unsigned i = 0; i < 6; i++) {
        tx_frame[i] = target[i];
        tx_frame[6 + i] = mac[i];
    }
    nw_put16(tx_frame + 12, type);
}
static int send_arp(uint16_t op, const uint8_t target[6], uint32_t target_ip) {
    ethernet(0x0806, target);
    uint8_t *a = tx_frame + 14;
    nw_put16(a, 1);
    nw_put16(a + 2, 0x0800);
    a[4] = 6;
    a[5] = 4;
    nw_put16(a + 6, op);
    for (unsigned i = 0; i < 6; i++) {
        a[8 + i] = mac[i];
        a[18 + i] = op == 1 ? 0 : target[i];
    }
    nw_put32(a + 14, ip);
    nw_put32(a + 24, target_ip);
    return emit(tx_frame, 42);
}
static void learn(uint32_t address, const uint8_t hw[6]) {
    unsigned slot = 0;
    uint32_t oldest = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (neighbors[i].valid && neighbors[i].ip == address) {
            slot = i;
            break;
        }
        if (!neighbors[i].valid) {
            slot = i;
            oldest = UINT32_MAX;
            continue;
        }
        uint32_t age = timer_ticks() - neighbors[i].stamp;
        if (age >= oldest) {
            oldest = age;
            slot = i;
        }
    }
    neighbors[slot].ip = address;
    neighbors[slot].stamp = timer_ticks();
    neighbors[slot].valid = 1;
    for (unsigned i = 0; i < 6; i++)
        neighbors[slot].mac[i] = hw[i];
}
static int neighbor(uint32_t target, uint8_t hw[6]) {
    for (unsigned i = 0; i < 8; i++) {
        struct neighbor *n = &neighbors[i];
        if (n->valid && timer_ticks() - n->stamp >= 6000)
            n->valid = 0;
        if (n->valid && n->ip == target) {
            for (unsigned j = 0; j < 6; j++)
                hw[j] = n->mac[j];
            return 1;
        }
    }
    return 0;
}
static int route(uint32_t target, uint8_t hw[6]) {
    if (target == BROADCAST) {
        for (unsigned i = 0; i < 6; i++)
            hw[i] = 255;
        return 0;
    }
    if (!configured)
        return -107;
    if (!nw_unicast(target))
        return -22;
    uint32_t hop = (target & mask) == (ip & mask) ? target : router;
    if (!hop)
        return -101;
    if (neighbor(hop, hw))
        return 0;
    arp_pending = hop;
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        arp_sent = timer_ticks();
        int error = send_arp(1, broadcast, hop);
        if (error && error != -11) {
            if (arp_pending == hop)
                arp_pending = 0;
            return error;
        }
        uint32_t deadline = timer_ticks() + 50;
        while (!expired(deadline)) {
            net_poll();
            if (neighbor(hop, hw)) {
                if (arp_pending == hop)
                    arp_pending = 0;
                return 0;
            }
            error = idle();
            if (error) {
                if (arp_pending == hop)
                    arp_pending = 0;
                return error;
            }
        }
    }
    if (arp_pending == hop)
        arp_pending = 0;
    return -110;
}
static int ip_mac(uint32_t src, uint32_t target, uint8_t protocol, const uint8_t *payload,
                  unsigned len, const uint8_t hw[6]) {
    if (len > 1480)
        return -7;
    ethernet(0x0800, hw);
    uint8_t *p = tx_frame + 14;
    for (unsigned i = 0; i < 20; i++)
        p[i] = 0;
    p[0] = 0x45;
    nw_put16(p + 2, len + 20);
    nw_put16(p + 4, ++ip_id);
    nw_put16(p + 6, 0x4000);
    p[8] = 64;
    p[9] = protocol;
    nw_put32(p + 12, src);
    nw_put32(p + 16, target);
    nw_put16(p + 10, nw_checksum(p, 20, 0));
    for (unsigned i = 0; i < len; i++)
        p[20 + i] = payload[i];
    return emit(tx_frame, 34 + len);
}
static int udp_send(uint32_t target, uint16_t source_port, uint16_t target_port,
                    const uint8_t *data, unsigned len) {
    if (!available || !rtl_link())
        return -100;
    if (len > 1472)
        return -7;
    uint8_t hw[6];
    int error = route(target, hw);
    if (error)
        return error;
    nw_put16(udp_tx, source_port);
    nw_put16(udp_tx + 2, target_port);
    nw_put16(udp_tx + 4, len + 8);
    nw_put16(udp_tx + 6, 0);
    for (unsigned i = 0; i < len; i++)
        udp_tx[8 + i] = data[i];
    uint16_t check = nw_checksum(udp_tx, len + 8, nw_pseudo(ip, target, len + 8));
    nw_put16(udp_tx + 6, check ? check : 65535);
    return ip_mac(ip, target, 17, udp_tx, len + 8, hw);
}
static int offer_valid(const struct dhcp_offer *o) {
    uint32_t host = ~o->mask;
    if (!nw_unicast(o->ip) || !nw_unicast(o->server) || !o->mask || host < 3 || (host & (host + 1)))
        return 0;
    if (!(o->ip & host) || (o->ip & host) == host)
        return 0;
    if (o->router && (!nw_unicast(o->router) || (o->router & o->mask) != (o->ip & o->mask) ||
                      o->router == o->ip))
        return 0;
    return !o->dns || nw_unicast(o->dns);
}
static void dhcp_receive(const uint8_t *p, unsigned len) {
    struct dhcp_offer answer;
    if (nw_dhcp(p, len, dhcp_xid, mac, &answer)) {
        drops++;
        return;
    }
    if (dhcp_phase == 1 && answer.type == 2 && offer_valid(&answer)) {
        offer = answer;
        dhcp_phase = 2;
    } else if (dhcp_phase == 3 && answer.server == offer.server) {
        if (answer.type == 6) {
            dhcp_phase = -99;
            return;
        }
        if (answer.type != 5 || answer.ip != offer.ip || !offer_valid(&answer)) {
            drops++;
            return;
        }
        offer = answer;
        ip = answer.ip;
        mask = answer.mask;
        router = answer.router;
        dns = answer.dns;
        uint32_t seconds = answer.lease ? answer.lease : 3600;
        forever = seconds == UINT32_MAX;
        if (seconds > 21474836u)
            seconds = 21474836u;
        lease_end = timer_ticks() + seconds * 100u;
        configured = 1;
        dhcp_phase = 4;
    }
}
static void udp_receive(uint32_t source, uint32_t destination, const uint8_t *p, unsigned len) {
    if (len < 8) {
        drops++;
        return;
    }
    unsigned size = nw16(p + 4);
    uint16_t from = nw16(p), to = nw16(p + 2);
    if (size < 8 || size > len ||
        (nw16(p + 6) && nw_checksum(p, size, nw_pseudo(source, destination, size)))) {
        drops++;
        return;
    }
    const uint8_t *data = p + 8;
    unsigned bytes = size - 8;
    if (from == 67 && to == 68 && (dhcp_phase == 1 || dhcp_phase == 3)) {
        dhcp_receive(data, bytes);
        return;
    }
    if (query.active && source == dns && from == 53 && to == query.port) {
        if (bytes < 2 || nw16(data) != query.id) {
            drops++;
            return;
        }
        int error = nw_dns_answer(data, bytes, query.name, query.id, &query.ip, &query.ttl);
        if (error == -22) {
            drops++;
            return;
        }
        query.result = error;
        query.active = 0;
        return;
    }
    if (transfer.active && source == transfer.server && to == transfer.port) {
        if ((transfer.peer && from != transfer.peer) || bytes < 4) {
            drops++;
            return;
        }
        unsigned op = nw16(data), block = nw16(data + 2);
        if (op == 5) {
            if (bytes < 5 || data[bytes - 1]) {
                drops++;
                return;
            }
            transfer.error = block == 1 ? -2 : block == 2 ? -13 : -5;
            return;
        }
        if (op != 3 || bytes > 516) {
            drops++;
            return;
        }
        if (block == transfer.expected) {
            if (!transfer.peer)
                transfer.peer = from;
            transfer.size = bytes - 4;
            for (unsigned i = 0; i < transfer.size; i++)
                transfer.data[i] = data[4 + i];
            transfer.ready = 1;
        } else if (transfer.expected > 1 && block == (uint16_t)(transfer.expected - 1))
            transfer.duplicate = 1;
        else
            drops++;
        return;
    }
    if (destination == ip && configured && !socket_udp_receive(source, from, to, data, bytes))
        return;
    drops++;
}
static void receive(const uint8_t *frame, unsigned len) {
    if (len < 14 || (!nw_equal(frame, mac, 6) && !nw_equal(frame, broadcast, 6))) {
        drops++;
        return;
    }
    unsigned type = nw16(frame + 12);
    if (type == 0x0806) {
        const uint8_t *a = frame + 14;
        if (len < 42 || nw16(a) != 1 || nw16(a + 2) != 0x0800 || a[4] != 6 || a[5] != 4 ||
            !nw_equal(a + 8, frame + 6, 6) || (a[8] & 1) || !nw_unicast(nw32(a + 14))) {
            drops++;
            return;
        }
        unsigned op = nw16(a + 6);
        uint32_t sender = nw32(a + 14), target = nw32(a + 24);
        if (!configured || target != ip)
            return;
        if (op == 1) {
            learn(sender, a + 8);
            send_arp(2, a + 8, sender);
        } else if (op == 2 && sender == arp_pending && nw_equal(a + 18, mac, 6)) {
            learn(sender, a + 8);
            arp_pending = 0;
        } else
            drops++;
        return;
    }
    if (type != 0x0800 || len < 34) {
        drops++;
        return;
    }
    const uint8_t *p = frame + 14;
    unsigned header = (p[0] & 15) * 4, total = nw16(p + 2);
    if ((p[0] >> 4) != 4 || header < 20 || header > len - 14 || total < header ||
        total > len - 14 || (nw16(p + 6) & 0xBFFF) || !p[8] || nw_checksum(p, header, 0)) {
        drops++;
        return;
    }
    uint32_t src = nw32(p + 12), dst = nw32(p + 16);
    if (!nw_unicast(src) || (dst != BROADCAST && (!configured || dst != ip))) {
        drops++;
        return;
    }
    p += header;
    unsigned bytes = total - header;
    if (frame[23] == 6 && configured && dst == ip) {
        if (socket_tcp_receive(src, dst, p, bytes))
            drops++;
        return;
    }
    if (frame[23] == 17) {
        udp_receive(src, dst, p, bytes);
        return;
    }
    if (frame[23] != 1 || !configured || dst != ip || bytes < 8 || nw_checksum(p, bytes, 0)) {
        drops++;
        return;
    }
    if (p[0] == 0 && p[1] == 0 && echo.active && src == echo.peer && nw16(p + 4) == 0x4D4F &&
        nw16(p + 6) == echo.sequence && bytes == 24 && nw_equal(p + 8, "MiniOS echo data", 16))
        echo.received = 1;
    else if (p[0] == 8 && p[1] == 0 && bytes <= sizeof(icmp)) {
        for (unsigned i = 0; i < bytes; i++)
            icmp[i] = p[i];
        icmp[0] = 0;
        nw_put16(icmp + 2, 0);
        nw_put16(icmp + 2, nw_checksum(icmp, bytes, 0));
        ip_mac(ip, src, 1, icmp, bytes, frame + 6);
    } else
        drops++;
}
int net_ready(void) {
    if (!available || !rtl_link())
        return -100;
    return configured ? 0 : -107;
}
uint32_t net_address(void) { return ip; }
int net_transport(uint32_t target, unsigned protocol, const uint8_t *p, unsigned len) {
    int error = net_ready();
    if (error)
        return error;
    if (!nw_unicast(target) || (protocol != 6 && protocol != 17))
        return -22;
    uint32_t hop = (target & mask) == (ip & mask) ? target : router;
    if (!hop)
        return -101;
    uint8_t hw[6];
    if (!neighbor(hop, hw)) {
        uint32_t now = timer_ticks();
        if (!arp_pending || (int32_t)(now - arp_sent) >= 50) {
            arp_pending = hop;
            arp_sent = now;
            error = send_arp(1, broadcast, hop);
            if (error && error != -11)
                return error;
        }
        return -11;
    }
    return ip_mac(ip, target, protocol, p, len, hw);
}
void net_init(void) {
    pci_init();
    available = rtl_init(mac) == 0;
    vga_puts(available ? "Network: RTL8139 ready (use dhcp)\n"
                       : "Network: no supported RTL8139; offline\n");
}
void net_poll(void) {
    if (!available) {
        socket_network_down(-100);
        return;
    }
    if (configured && !forever && expired(lease_end)) {
        configured = 0;
        ip = mask = router = dns = 0;
        for (unsigned i = 0; i < 8; i++)
            neighbors[i].valid = 0;
    }
    for (unsigned i = 0; i < 8; i++) {
        int len = rtl_receive(rx_frame, sizeof(rx_frame));
        if (!len)
            break;
        if (len < 0) {
            drops++;
            break;
        }
        rx_count++;
        receive(rx_frame, len);
    }
    socket_service();
}
static int dhcp_send(unsigned type) {
    uint8_t p[320] = {0};
    p[0] = 1;
    p[1] = 1;
    p[2] = 6;
    nw_put32(p + 4, dhcp_xid);
    nw_put16(p + 10, 0x8000);
    for (unsigned i = 0; i < 6; i++)
        p[28 + i] = mac[i];
    nw_put32(p + 236, 0x63825363);
    unsigned n = 240;
    p[n++] = 53;
    p[n++] = 1;
    p[n++] = type;
    p[n++] = 61;
    p[n++] = 7;
    p[n++] = 1;
    for (unsigned i = 0; i < 6; i++)
        p[n++] = mac[i];
    if (type == 3) {
        p[n++] = 50;
        p[n++] = 4;
        nw_put32(p + n, offer.ip);
        n += 4;
        p[n++] = 54;
        p[n++] = 4;
        nw_put32(p + n, offer.server);
        n += 4;
    }
    p[n++] = 55;
    p[n++] = 5;
    p[n++] = 1;
    p[n++] = 3;
    p[n++] = 6;
    p[n++] = 51;
    p[n++] = 54;
    p[n++] = 255;
    return udp_send(BROADCAST, 68, 67, p, n < 300 ? 300 : n);
}
int net_dhcp(void) {
    if (!available || !rtl_link())
        return -100;
    socket_network_down(-102);
    configured = 0;
    arp_pending = 0;
    ip = mask = router = dns = 0;
    for (unsigned i = 0; i < 8; i++)
        neighbors[i].valid = 0;
    for (unsigned i = 0; i < 4; i++)
        dns_cache[i].valid = 0;
    dhcp_xid = 0x4D4F0000u ^ timer_ticks() ^ (++transaction);
    dhcp_phase = 1;
    for (unsigned stage = 0; stage < 2; stage++) {
        int waiting = stage ? 3 : 1;
        if (stage)
            dhcp_phase = 3;
        for (unsigned attempt = 0; attempt < 3 && dhcp_phase == waiting; attempt++) {
            int error = dhcp_send(stage ? 3 : 1);
            if (error && error != -11) {
                dhcp_phase = 0;
                return error;
            }
            uint32_t deadline = timer_ticks() + 100;
            while (dhcp_phase == waiting && !expired(deadline)) {
                error = idle();
                if (error) {
                    dhcp_phase = 0;
                    return error;
                }
            }
        }
        if (dhcp_phase < 0) {
            int error = dhcp_phase;
            dhcp_phase = 0;
            return error;
        }
        if (dhcp_phase == waiting) {
            dhcp_phase = 0;
            return -110;
        }
    }
    dhcp_phase = 0;
    if (!configured)
        return -99;
    vga_puts("DHCP bound ");
    print_ip(ip);
    vga_putc('\n');
    return 0;
}
int net_ping(const char *address) {
    uint32_t target;
    if (!nw_ip(address, &target) || !nw_unicast(target))
        return -22;
    if (!configured)
        return -107;
    uint8_t hw[6];
    int error = route(target, hw);
    if (error)
        return error;
    unsigned received = 0;
    for (unsigned i = 0; i < 3; i++) {
        uint8_t p[24] = {8, 0};
        nw_put16(p + 4, 0x4D4F);
        nw_put16(p + 6, ++transaction);
        for (unsigned j = 0; j < 16; j++)
            p[8 + j] = "MiniOS echo data"[j];
        nw_put16(p + 2, nw_checksum(p, sizeof(p), 0));
        echo.active = 1;
        echo.received = 0;
        echo.peer = target;
        echo.sequence = transaction;
        echo.sent = timer_ticks();
        error = ip_mac(ip, target, 1, p, sizeof(p), hw);
        if (error) {
            echo.active = 0;
            return error;
        }
        uint32_t deadline = timer_ticks() + 100;
        while (!echo.received && !expired(deadline)) {
            error = idle();
            if (error) {
                echo.active = 0;
                return error;
            }
        }
        if (echo.received) {
            received++;
            vga_puts("Reply from ");
            print_ip(target);
            vga_puts(" ticks=");
            vga_put_dec(timer_ticks() - echo.sent);
            vga_putc('\n');
        } else
            vga_puts("Echo request timed out.\n");
        echo.active = 0;
    }
    vga_puts("Ping received=");
    vga_put_dec(received);
    vga_puts("/3\n");
    return received ? 0 : -110;
}
int net_resolve(const char *name) {
    if (!configured || !dns)
        return -107;
    uint8_t packet[512];
    uint16_t id = ++transaction;
    int size = nw_dns_query(packet, sizeof(packet), name, id);
    if (size < 0)
        return size;
    unsigned length = 0;
    while (name[length])
        length++;
    for (unsigned i = 0; i < 4; i++)
        if (dns_cache[i].valid && !expired(dns_cache[i].expires) &&
            nw_equal(name, dns_cache[i].name, length + 1)) {
            vga_puts("DNS cached ");
            vga_puts(name);
            vga_puts(" = ");
            print_ip(dns_cache[i].ip);
            vga_putc('\n');
            return 0;
        }
    query.port = ++port_counter;
    if (port_counter < 49152)
        query.port = port_counter = 49152;
    query.id = id;
    for (unsigned i = 0; i <= length; i++)
        query.name[i] = name[i];
    query.result = -110;
    query.active = 1;
    for (unsigned attempt = 0; attempt < 3 && query.active; attempt++) {
        int error = udp_send(dns, query.port, 53, packet, size);
        if (error) {
            query.active = 0;
            return error;
        }
        uint32_t deadline = timer_ticks() + 100;
        while (query.active && !expired(deadline)) {
            error = idle();
            if (error) {
                query.active = 0;
                return error;
            }
        }
    }
    query.active = 0;
    if (query.result)
        return query.result;
    unsigned slot = cache_slot++ % 4;
    for (unsigned i = 0; i <= length; i++)
        dns_cache[slot].name[i] = name[i];
    dns_cache[slot].ip = query.ip;
    dns_cache[slot].valid = 1;
    dns_cache[slot].expires = timer_ticks() + (query.ttl > 3600 ? 3600 : query.ttl) * 100;
    vga_puts("DNS ");
    vga_puts(name);
    vga_puts(" = ");
    print_ip(query.ip);
    vga_putc('\n');
    return 0;
}
static int remote_name(const char *p) {
    unsigned n = 0, component = 0;
    if (!*p || *p == '/')
        return 0;
    while (*p) {
        char c = *p++;
        if (++n > 63)
            return 0;
        if (c == '/') {
            if (!component)
                return 0;
            component = 0;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
            return 0;
        if (c == '.' && !component && (*p == '.' || *p == '/' || !*p))
            return 0;
        component++;
    }
    return component != 0;
}
int net_fetch(const char *address, const char *remote, const char *destination) {
    uint32_t server;
    if (!nw_ip(address, &server) || !nw_unicast(server) || !remote_name(remote))
        return -22;
    if (!configured)
        return -107;
    struct fs_stat existing;
    int status = fs_stat(destination, &existing);
    if (!status)
        return -17;
    if (status != -2)
        return status;
    uint8_t *file = kmalloc(DOWNLOAD_MAX);
    if (!file)
        return -12;
    uint8_t request[80];
    nw_put16(request, 1);
    unsigned n = 2;
    while (*remote)
        request[n++] = *remote++;
    request[n++] = 0;
    const char mode[] = "octet";
    for (unsigned i = 0; i < sizeof(mode); i++)
        request[n++] = mode[i];
    transfer =
        (typeof(transfer)){.active = 1, .port = ++port_counter, .expected = 1, .server = server};
    if (port_counter < 49152)
        transfer.port = port_counter = 49152;
    unsigned total = 0, retries = 0;
    int error = udp_send(server, transfer.port, 69, request, n);
    uint32_t deadline = timer_ticks() + 100, operation_end = timer_ticks() + 3000;
    while (!error) {
        net_poll();
        if (expired(operation_end)) {
            error = -110;
            break;
        }
        if (transfer.error) {
            error = transfer.error;
            break;
        }
        if (transfer.ready) {
            unsigned size = transfer.size;
            transfer.ready = 0;
            if (size > DOWNLOAD_MAX - total) {
                error = -27;
                break;
            }
            for (unsigned i = 0; i < size; i++)
                file[total + i] = transfer.data[i];
            total += size;
            uint8_t ack[4];
            nw_put16(ack, 4);
            nw_put16(ack + 2, transfer.expected++);
            error = udp_send(server, transfer.port, transfer.peer, ack, 4);
            if (error)
                break;
            retries = 0;
            deadline = timer_ticks() + 100;
            if (size < 512)
                break;
        } else if (transfer.duplicate) {
            transfer.duplicate = 0;
            uint8_t ack[4];
            nw_put16(ack, 4);
            nw_put16(ack + 2, transfer.expected - 1);
            error = udp_send(server, transfer.port, transfer.peer, ack, 4);
        } else if (expired(deadline)) {
            if (++retries == 3) {
                error = -110;
                break;
            }
            if (transfer.expected == 1)
                error = udp_send(server, transfer.port, 69, request, n);
            else {
                uint8_t ack[4];
                nw_put16(ack, 4);
                nw_put16(ack + 2, transfer.expected - 1);
                error = udp_send(server, transfer.port, transfer.peer, ack, 4);
            }
            deadline = timer_ticks() + 100;
        } else
            error = idle();
    }
    transfer.active = 0;
    if (!error) {
        struct file_handle out = {0};
        error = fs_open(&out, destination, FS_WRITE | FS_CREATE | FS_EXCL);
        if (!error) {
            int wrote = fs_write(&out, file, total), closed = fs_close(&out);
            if (wrote != (int)total || closed) {
                error = wrote < 0 ? wrote : closed ? closed : -5;
                fs_unlink(destination);
            }
        }
    }
    kfree(file);
    heap_trim();
    if (!error) {
        vga_puts("Fetched bytes=");
        vga_put_dec(total);
        vga_puts(" to ");
        vga_puts(destination);
        vga_putc('\n');
    }
    return error;
}
void net_info(void) {
    vga_puts("Network: ");
    vga_puts(available ? "RTL8139 " : "offline ");
    vga_puts(rtl_link() ? "link-up\n" : "link-down\n");
    vga_puts("MAC ");
    const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 6; i++) {
        if (i)
            vga_putc(':');
        vga_putc(hex[mac[i] >> 4]);
        vga_putc(hex[mac[i] & 15]);
    }
    vga_puts(configured ? " configured\n" : " unconfigured\n");
    vga_puts("IP ");
    print_ip(ip);
    vga_puts(" mask ");
    print_ip(mask);
    vga_putc('\n');
    vga_puts("Gateway ");
    print_ip(router);
    vga_puts(" DNS ");
    print_ip(dns);
    vga_putc('\n');
    vga_puts("RX=");
    vga_put_dec(rx_count);
    vga_puts(" TX=");
    vga_put_dec(tx_count);
    vga_puts(" dropped=");
    vga_put_dec(drops);
    vga_puts(" driver-errors=");
    vga_put_dec(rtl_errors());
    vga_putc('\n');
}
void net_arp_list(void) {
    vga_puts("ARP neighbors (60 second lifetime):\n");
    for (unsigned i = 0; i < 8; i++)
        if (neighbors[i].valid && timer_ticks() - neighbors[i].stamp < 6000) {
            print_ip(neighbors[i].ip);
            vga_puts(" age-ticks=");
            vga_put_dec(timer_ticks() - neighbors[i].stamp);
            vga_putc('\n');
        }
}
