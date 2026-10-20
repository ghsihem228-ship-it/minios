/* Bounded, single-CPU nonblocking sockets. Syscalls never poll hardware or
 * retain a user pointer. Protocol work happens only in task 0's net_poll. */
#include "socket.h"
#include "tcp_wire.h"
#include "net_wire.h"
#include "net.h"
#include "timer.h"
#include "vga.h"
#define SOCKET_COUNT 8
#define RX_CAP 2048u
#define TCP_MSS 512u
#define RETRIES 4u
#define RTO 100u
#define DEADLINE 600u
#define QUIET 200u
/* Only active TCP opens. CLOSED retains an error until the owner closes it. */
enum state {
    FREE,
    OPEN,
    SYN_SENT,
    ESTABLISHED,
    CLOSE_WAIT,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSING,
    LAST_ACK,
    TIME_WAIT,
    CLOSED,
    ABORTING
};
struct socket {
    uint32_t owner, peer, una, next, receive, flight_seq, last, deadline, persist;
    unsigned fd, type, state, rx_used, rx_head, tx_size, tx_acked, flight_len, attempts;
    uint16_t local, remote, window, mss, udp_len[2];
    unsigned udp_head, udp_count, udp_pending, flight_flags, flight_sent, ack_pending;
    int error, eof, closing;
    uint8_t rx[RX_CAP], tx[SOCK_IO_MAX];
};
static struct socket sockets[SOCKET_COUNT];
static uint8_t packet[SOCK_IO_MAX + 24];
static uint16_t next_port = 32768;
static uint32_t serial = 0x4d4f0000u, received, transmitted, retransmits, rejected;
static int due(uint32_t deadline) { return (int32_t)(timer_ticks() - deadline) >= 0; }
static int before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static struct socket *find(uint32_t owner, unsigned fd) {
    if (!owner || fd >= SOCK_FDS)
        return 0;
    for (unsigned i = 0; i < SOCKET_COUNT; i++)
        if (sockets[i].state != FREE && sockets[i].owner == owner && sockets[i].fd == fd)
            return &sockets[i];
    return 0;
}
static int port_used(uint16_t port) {
    for (unsigned i = 0; i < SOCKET_COUNT; i++)
        if (sockets[i].state != FREE && sockets[i].local == port)
            return 1;
    return 0;
}
static uint16_t allocate_port(void) {
    for (unsigned n = 0; n < 16384; n++) {
        uint16_t port = next_port++;
        if (next_port >= 49152)
            next_port = 32768;
        if (!port_used(port))
            return port;
    }
    return 0;
}
static void fail(struct socket *s, int error) {
    s->error = error;
    s->state = CLOSED;
    s->flight_flags = s->tx_size = s->udp_pending = 0;
    if (!s->owner)
        s->state = FREE;
}
int socket_open(uint32_t owner, unsigned type) {
    if (!owner)
        return -1;
    if (type != SOCK_UDP && type != SOCK_TCP)
        return -22;
    unsigned fd;
    for (fd = 0; fd < SOCK_FDS && find(owner, fd); fd++)
        ;
    if (fd == SOCK_FDS)
        return -24;
    for (unsigned i = 0; i < SOCKET_COUNT; i++)
        if (sockets[i].state == FREE) {
            struct socket *s = &sockets[i];
            *s = (struct socket){0};
            s->owner = owner;
            s->fd = fd;
            s->type = type;
            s->state = OPEN;
            s->mss = TCP_MSS;
            s->local = allocate_port();
            if (!s->local) {
                s->state = FREE;
                return -24;
            }
            return fd;
        }
    return -24;
}
int socket_bind(uint32_t owner, unsigned fd, uint32_t port) {
    struct socket *s = find(owner, fd);
    if (!s)
        return -9;
    if (s->type != SOCK_UDP || s->state != OPEN || port < 1024 || port >= 49152)
        return -22;
    if (port != s->local && port_used(port))
        return -98;
    s->local = port;
    return 0;
}
int socket_connect(uint32_t owner, unsigned fd, uint32_t address, uint32_t port) {
    struct socket *s = find(owner, fd);
    if (!s)
        return -9;
    if (!nw_unicast(address) || !port || port > 65535)
        return -22;
    if (s->state != OPEN)
        return s->state == SYN_SENT ? -114 : -106;
    int error = net_ready();
    if (error)
        return error;
    s->peer = address;
    s->remote = port;
    if (s->type == SOCK_UDP) {
        s->state = ESTABLISHED;
        return 0;
    }
    s->una = s->next = (serial += 0x10001u) + timer_ticks() * 317u;
    s->flight_seq = s->una;
    s->flight_flags = TCP_SYN;
    s->state = SYN_SENT;
    s->deadline = timer_ticks() + DEADLINE;
    return -115;
}
int socket_send(uint32_t owner, unsigned fd, const uint8_t *p, unsigned len) {
    struct socket *s = find(owner, fd);
    if (!s)
        return -9;
    if (len > SOCK_IO_MAX)
        return -7;
    if (s->error)
        return s->error;
    if ((s->state != ESTABLISHED && s->state != CLOSE_WAIT) || s->closing)
        return -107;
    if (s->tx_size || s->udp_pending)
        return -11;
    if (s->type == SOCK_TCP && !len)
        return 0;
    for (unsigned i = 0; i < len; i++)
        s->tx[i] = p[i];
    s->tx_size = len;
    s->tx_acked = 0;
    s->udp_pending = s->type == SOCK_UDP;
    s->deadline = timer_ticks() + DEADLINE;
    s->persist = timer_ticks() + RTO;
    return len;
}
int socket_recv(uint32_t owner, unsigned fd, uint8_t *p, unsigned len) {
    struct socket *s = find(owner, fd);
    if (!s)
        return -9;
    if (len > SOCK_IO_MAX)
        return -7;
    if (!len)
        return 0;
    if (s->type == SOCK_UDP && s->udp_count) {
        unsigned slot = s->udp_head, size = s->udp_len[slot];
        if (size > len)
            size = len;
        for (unsigned i = 0; i < size; i++)
            p[i] = s->rx[slot * SOCK_IO_MAX + i];
        s->udp_head = (slot + 1) % 2;
        s->udp_count--;
        return size;
    }
    if (s->type == SOCK_TCP && s->rx_used) {
        if (len > s->rx_used)
            len = s->rx_used;
        for (unsigned i = 0; i < len; i++)
            p[i] = s->rx[(s->rx_head + i) % RX_CAP];
        s->rx_head = (s->rx_head + len) % RX_CAP;
        s->rx_used -= len;
        s->ack_pending = 1;
        return len;
    }
    if (s->error)
        return s->error;
    if (s->eof)
        return 0;
    return -11;
}
int socket_status(uint32_t owner, unsigned fd) {
    struct socket *s = find(owner, fd);
    if (!s)
        return -9;
    if (s->error)
        return s->error;
    unsigned flags = s->state == SYN_SENT ? SOCK_CONNECTING : 0;
    if (s->state == ESTABLISHED || s->state == CLOSE_WAIT) {
        flags |= SOCK_CONNECTED;
        if (!s->tx_size && !s->udp_pending && !s->closing)
            flags |= SOCK_WRITABLE;
    }
    if (s->rx_used || s->udp_count)
        flags |= SOCK_READABLE;
    if (s->eof)
        flags |= SOCK_EOF;
    return flags;
}
int socket_close(uint32_t owner, unsigned fd) {
    struct socket *s = find(owner, fd);
    if (!s)
        return -9;
    s->owner = 0;
    if (s->type == SOCK_UDP || s->state == OPEN || s->state == CLOSED) {
        s->state = FREE;
        return 0;
    }
    if (s->state == SYN_SENT) {
        s->state = ABORTING;
        s->deadline = timer_ticks() + 50;
        return 0;
    }
    s->closing = 1;
    s->deadline = timer_ticks() + DEADLINE;
    return 0;
}
void socket_reap(uint32_t owner) {
    for (unsigned i = 0; i < SOCKET_COUNT; i++) {
        struct socket *s = &sockets[i];
        if (s->state == FREE || s->owner != owner)
            continue;
        s->owner = 0;
        if (s->type == SOCK_TCP && s->state != OPEN && s->state != CLOSED) {
            s->state = ABORTING;
            s->deadline = timer_ticks() + 50;
        } else
            s->state = FREE;
    }
}
void socket_network_down(int error) {
    for (unsigned i = 0; i < SOCKET_COUNT; i++) {
        struct socket *s = &sockets[i];
        if (s->state != FREE && s->state != OPEN && s->state != CLOSED)
            fail(s, error);
    }
}
static int segment(struct socket *s, unsigned flags, uint32_t seq, const uint8_t *data,
                   unsigned len) {
    unsigned header = (flags & TCP_SYN) ? 24 : 20;
    for (unsigned i = 0; i < header; i++)
        packet[i] = 0;
    nw_put16(packet, s->local);
    nw_put16(packet + 2, s->remote);
    nw_put32(packet + 4, seq);
    nw_put32(packet + 8, (flags & TCP_ACK) ? s->receive : 0);
    packet[12] = (header / 4) << 4;
    packet[13] = flags;
    nw_put16(packet + 14, RX_CAP - s->rx_used);
    if (header == 24) {
        packet[20] = 2;
        packet[21] = 4;
        nw_put16(packet + 22, TCP_MSS);
    }
    for (unsigned i = 0; i < len; i++)
        packet[header + i] = data[i];
    nw_put16(packet + 16,
             nw_checksum(packet, header + len, tcp_pseudo(net_address(), s->peer, header + len)));
    int error = net_transport(s->peer, 6, packet, header + len);
    if (!error) {
        transmitted++;
        s->ack_pending = 0;
    }
    return error;
}
static void quiet(struct socket *s) {
    s->state = TIME_WAIT;
    s->deadline = timer_ticks() + QUIET;
}
/* Incoming TCP has already passed IP routing/fragment/header validation. */
int socket_tcp_receive(uint32_t source, uint32_t destination, const uint8_t *p, unsigned len) {
    struct tcp_packet v;
    if (tcp_decode(source, destination, p, len, &v)) {
        rejected++;
        return -22;
    }
    struct socket *s = 0;
    for (unsigned i = 0; i < SOCKET_COUNT; i++) {
        struct socket *candidate = &sockets[i];
        if (candidate->state != FREE && candidate->type == SOCK_TCP &&
            candidate->local == v.destination && candidate->remote == v.source &&
            candidate->peer == source) {
            s = candidate;
            break;
        }
    }
    if (!s) {
        rejected++;
        return -2;
    }
    received++;
    if (s->state == SYN_SENT) {
        if ((v.flags & TCP_ACK) && v.ack == s->next && s->flight_sent) {
            if (v.flags & TCP_RST) {
                fail(s, -111);
                return 0;
            }
            if ((v.flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) && !v.length) {
                s->una = v.ack;
                s->receive = v.seq + 1;
                s->window = v.window;
                s->mss = v.mss < TCP_MSS ? v.mss : TCP_MSS;
                s->state = ESTABLISHED;
                s->flight_flags = s->flight_sent = s->attempts = 0;
                s->ack_pending = 1;
                return 0;
            }
        }
        rejected++;
        return -22;
    }
    if (s->state == CLOSED || s->state == ABORTING || s->state == OPEN)
        return 0;
    if (v.flags & TCP_RST) {
        if (v.seq == s->receive)
            fail(s, -104);
        else {
            s->ack_pending = 1;
            rejected++;
        }
        return 0;
    }
    if (v.flags & TCP_SYN) {
        if (v.seq + 1 == s->receive)
            s->ack_pending = 1;
        rejected++;
        return -22;
    }
    if (!(v.flags & TCP_ACK) || before(s->next, v.ack)) {
        s->ack_pending = 1;
        rejected++;
        return -22;
    }
    /* Drop segments outside our receive window before accepting ACK/window updates. */
    if (before(s->receive, v.seq) ||
        before(v.seq + v.length + (!!(v.flags & TCP_FIN)), s->receive)) {
        s->ack_pending = 1;
        rejected++;
        return -22;
    }
    s->window = v.window;
    if (before(s->una, v.ack)) {
        unsigned advance = v.ack - s->una;
        if (s->flight_flags & TCP_FIN) {
            if (v.ack != s->next) {
                rejected++;
                return -22;
            }
            s->flight_flags = s->flight_sent = 0;
            if (s->state == LAST_ACK) {
                s->state = FREE;
                return 0;
            }
            if (s->state == CLOSING)
                quiet(s);
            else if (s->state == FIN_WAIT_1)
                s->state = FIN_WAIT_2;
        } else if (s->flight_flags) {
            if (advance > s->flight_len) {
                rejected++;
                return -22;
            }
            s->tx_acked += advance;
            s->flight_len -= advance;
            s->flight_seq = v.ack;
            if (!s->flight_len) {
                s->flight_flags = s->flight_sent = 0;
                if (s->tx_acked == s->tx_size)
                    s->tx_size = s->tx_acked = 0;
            }
        }
        s->una = v.ack;
        s->attempts = 0;
        s->last = timer_ticks();
        s->deadline = timer_ticks() + DEADLINE;
    }
    unsigned skip = s->receive - v.seq;
    if (skip >
        v.length) { /* Duplicate FIN in TIME_WAIT/CLOSE_WAIT: re-ACK without consuming again. */
        if (v.flags & TCP_FIN) {
            s->ack_pending = 1;
            if (s->state == TIME_WAIT)
                s->deadline = timer_ticks() + QUIET;
        }
        return 0;
    }
    unsigned fresh = v.length - skip;
    if (s->eof && fresh) {
        s->ack_pending = 1;
        rejected++;
        return -22;
    }
    if (fresh > RX_CAP - s->rx_used) {
        s->ack_pending = 1;
        rejected++;
        return -11;
    }
    for (unsigned i = 0; i < fresh; i++)
        s->rx[(s->rx_head + s->rx_used + i) % RX_CAP] = v.data[skip + i];
    s->rx_used += fresh;
    s->receive += fresh;
    if (v.length)
        s->ack_pending = 1;
    if ((v.flags & TCP_FIN) && !s->eof) {
        s->eof = 1;
        s->receive++;
        s->ack_pending = 1;
        if (s->state == ESTABLISHED)
            s->state = CLOSE_WAIT;
        else if (s->state == FIN_WAIT_1)
            s->state = CLOSING;
        else if (s->state == FIN_WAIT_2)
            quiet(s);
    }
    return 0;
}
int socket_udp_receive(uint32_t address, uint16_t from, uint16_t to, const uint8_t *p,
                       unsigned len) {
    for (unsigned i = 0; i < SOCKET_COUNT; i++) {
        struct socket *s = &sockets[i];
        if (s->type != SOCK_UDP || s->state != ESTABLISHED || s->peer != address ||
            s->remote != from || s->local != to)
            continue;
        if (len > SOCK_IO_MAX || s->udp_count == 2) {
            rejected++;
            return -11;
        }
        unsigned slot = (s->udp_head + s->udp_count) % 2;
        for (unsigned j = 0; j < len; j++)
            s->rx[slot * SOCK_IO_MAX + j] = p[j];
        s->udp_len[slot] = len;
        s->udp_count++;
        received++;
        return 0;
    }
    rejected++;
    return -2;
}
void socket_service(void) {
    int status = net_ready();
    if (status)
        socket_network_down(status);
    for (unsigned i = 0; i < SOCKET_COUNT; i++) {
        struct socket *s = &sockets[i];
        if (s->state == FREE || s->state == OPEN || s->state == CLOSED)
            continue;
        if (s->state == TIME_WAIT) {
            if (s->ack_pending)
                segment(s, TCP_ACK, s->next, 0, 0);
            if (due(s->deadline))
                s->state = FREE;
            continue;
        }
        if (s->state == ABORTING) {
            int error = segment(s, TCP_RST | (s->receive ? TCP_ACK : 0), s->next, 0, 0);
            if (error != -11 || due(s->deadline))
                s->state = FREE;
            continue;
        }
        if (status)
            continue;
        if (s->type == SOCK_UDP) {
            if (!s->udp_pending)
                continue;
            nw_put16(packet, s->local);
            nw_put16(packet + 2, s->remote);
            nw_put16(packet + 4, s->tx_size + 8);
            nw_put16(packet + 6, 0);
            for (unsigned j = 0; j < s->tx_size; j++)
                packet[8 + j] = s->tx[j];
            uint16_t sum = nw_checksum(packet, s->tx_size + 8,
                                       nw_pseudo(net_address(), s->peer, s->tx_size + 8));
            nw_put16(packet + 6, sum ? sum : 65535);
            int error = net_transport(s->peer, 17, packet, s->tx_size + 8);
            if (!error) {
                s->udp_pending = s->tx_size = 0;
                transmitted++;
            } else if (error != -11)
                fail(s, error);
            else if (due(s->deadline))
                fail(s, -110);
            continue;
        }
        if ((s->state == SYN_SENT || s->closing || s->tx_size) && due(s->deadline)) {
            fail(s, -110);
            continue;
        }
        if (s->ack_pending) {
            int error = segment(s, TCP_ACK, s->next, 0, 0);
            if (error && error != -11) {
                fail(s, error);
                continue;
            }
        }
        if (!s->flight_flags && s->tx_size) {
            unsigned size = s->tx_size - s->tx_acked;
            if (size > s->mss)
                size = s->mss;
            if (size > s->window)
                size = s->window;
            if (size) {
                s->flight_flags = TCP_ACK | TCP_PSH;
                s->flight_len = size;
                s->flight_seq = s->next;
                s->flight_sent = s->attempts = 0;
            } else if (due(s->persist)) {
                /* A sequence just before the receive edge elicits a current window ACK. */
                segment(s, TCP_ACK, s->next - 1, 0, 0);
                s->persist = timer_ticks() + RTO;
            }
        }
        if (!s->flight_flags && s->closing && !s->tx_size &&
            (s->state == ESTABLISHED || s->state == CLOSE_WAIT)) {
            s->flight_flags = TCP_ACK | TCP_FIN;
            s->flight_len = 0;
            s->flight_seq = s->next;
            s->flight_sent = s->attempts = 0;
            s->state = s->state == CLOSE_WAIT ? LAST_ACK : FIN_WAIT_1;
        }
        if (!s->flight_flags)
            continue;
        if (s->flight_sent && !due(s->last + RTO))
            continue;
        if (s->attempts == RETRIES) {
            fail(s, -110);
            continue;
        }
        int error = segment(s, s->flight_flags, s->flight_seq, s->tx + s->tx_acked, s->flight_len);
        if (!error) {
            if (s->flight_sent)
                retransmits++;
            else
                s->next = s->flight_seq + s->flight_len + !!(s->flight_flags & (TCP_SYN | TCP_FIN));
            s->flight_sent = 1;
            s->last = timer_ticks();
            s->attempts++;
        } else if (error != -11)
            fail(s, error);
    }
}
void socket_list(void) {
    static const char *names[] = {"free",       "open",       "syn-sent",   "established",
                                  "close-wait", "fin-wait-1", "fin-wait-2", "closing",
                                  "last-ack",   "time-wait",  "closed",     "aborting"};
    unsigned active = 0;
    for (unsigned i = 0; i < SOCKET_COUNT; i++)
        if (sockets[i].state != FREE)
            active++;
    vga_puts("Sockets active=");
    vga_put_dec(active);
    vga_puts("/8 RX=");
    vga_put_dec(received);
    vga_puts(" TX=");
    vga_put_dec(transmitted);
    vga_puts(" retries=");
    vga_put_dec(retransmits);
    vga_puts(" rejected=");
    vga_put_dec(rejected);
    vga_putc('\n');
    vga_puts("PID FD TYPE STATE LOCAL PEER RX TX\n");
    for (unsigned i = 0; i < SOCKET_COUNT; i++) {
        struct socket *s = &sockets[i];
        if (s->state == FREE)
            continue;
        vga_put_dec(s->owner);
        vga_putc(' ');
        vga_put_dec(s->fd);
        vga_puts(s->type == SOCK_TCP ? " tcp " : " udp ");
        vga_puts(names[s->state]);
        vga_putc(' ');
        vga_put_dec(s->local);
        vga_putc(' ');
        vga_put_dec(s->remote);
        vga_putc(' ');
        vga_put_dec(s->rx_used + s->udp_count);
        vga_putc(' ');
        vga_put_dec(s->tx_size);
        vga_putc('\n');
    }
}
