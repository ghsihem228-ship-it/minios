#ifndef NET_H
#define NET_H
#include <stdint.h>
void net_init(void);
void net_poll(void); /* task 0 only, bounded; never from IRQ/syscall */
void net_info(void);
void net_arp_list(void);
int net_ready(void);
uint32_t net_address(void);
/* Task 0 only: nonblocking ARP/transport output; -11 asks caller to retry. */
int net_transport(uint32_t target, unsigned protocol, const uint8_t *p, unsigned len);
int net_dhcp(void);
int net_ping(const char *address);
int net_resolve(const char *name);
int net_fetch(const char *server, const char *remote, const char *destination);
#endif
