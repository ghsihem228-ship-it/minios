#ifndef SOCKET_H
#define SOCKET_H
#include <stdint.h>
#define SOCK_UDP 1
#define SOCK_TCP 2
#define SOCK_FDS 4
#define SOCK_IO_MAX 1024u
#define SOCK_CONNECTED 1u
#define SOCK_READABLE 2u
#define SOCK_WRITABLE 4u
#define SOCK_EOF 8u
#define SOCK_CONNECTING 16u
/* Syscall entry points only copy bounded data or change state. No NIC I/O. */
int socket_open(uint32_t owner, unsigned type);
int socket_bind(uint32_t owner, unsigned fd, uint32_t port);
int socket_connect(uint32_t owner, unsigned fd, uint32_t address, uint32_t port);
int socket_send(uint32_t owner, unsigned fd, const uint8_t *data, unsigned len);
int socket_recv(uint32_t owner, unsigned fd, uint8_t *data, unsigned len);
int socket_status(uint32_t owner, unsigned fd);
int socket_close(uint32_t owner, unsigned fd);
void socket_reap(uint32_t owner);
/* Network service and receive callbacks: task 0 only. */
void socket_service(void);
void socket_network_down(int error);
int socket_udp_receive(uint32_t address, uint16_t from, uint16_t to, const uint8_t *p,
                       unsigned len);
int socket_tcp_receive(uint32_t source, uint32_t destination, const uint8_t *p, unsigned len);
void socket_list(void);
#endif
