#include "minios.h"
static void usage(void) { printf("Usage: run /bin/http [<IPv4> <port> </path>]\n"); }
static int pause_until(uint32_t end) {
    if ((int32_t)(sys_ticks() - end) >= 0)
        return -110;
    return sys_sleep(1);
}
int main(int argc, char **argv) {
    const char *address = "10.0.2.2", *path = "/welcome.txt";
    uint32_t ip, port = 8080;
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage();
        return 0;
    }
    if (argc != 1 && argc != 4) {
        usage();
        return 2;
    }
    if (argc == 4) {
        address = argv[1];
        path = argv[3];
        if (!parse_u32(argv[2], &port) || !port || port > 65535) {
            usage();
            return 2;
        }
    }
    if (!parse_ipv4(address, &ip) || path[0] != '/') {
        usage();
        return 2;
    }
    for (const char *p = path; *p; p++)
        if ((unsigned char)*p < 33 || (unsigned char)*p > 126) {
            usage();
            return 2;
        }
    char request[256], buffer[256];
    int size = snprintf(request, sizeof(request),
                        "GET %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n", path,
                        address, (unsigned)port);
    if (size < 0 || size >= (int)sizeof(request)) {
        usage();
        return 2;
    }
    int fd = sys_socket(SOCK_TCP), error = fd;
    if (fd < 0)
        goto failed;
    error = sys_connect(fd, ip, port);
    if (error != -EINPROGRESS)
        goto failed;
    uint32_t end = sys_ticks() + 1000;
    for (;;) {
        error = sys_socket_status(fd);
        if (error < 0)
            goto failed;
        if (error & SOCK_CONNECTED)
            break;
        error = pause_until(end);
        if (error)
            goto failed;
    }
    error = sys_send(fd, request, (size_t)size);
    if (error != size) {
        if (error >= 0)
            error = -5;
        goto failed;
    }
    uint32_t bytes = 0, hash = 0x811c9dc5u;
    for (;;) {
        if ((int32_t)(sys_ticks() - end) >= 0) {
            error = -110;
            goto failed;
        }
        int n = sys_recv(fd, buffer, sizeof(buffer));
        if (n == -EAGAIN) {
            error = pause_until(end);
            if (error)
                goto failed;
            continue;
        }
        if (n < 0) {
            error = n;
            goto failed;
        }
        if (!n)
            break;
        if ((unsigned)n > 1024u * 1024u - bytes) {
            error = -27;
            goto failed;
        }
        for (int i = 0; i < n; i++)
            hash = (hash ^ (uint8_t)buffer[i]) * 0x01000193u;
        bytes += (unsigned)n;
        error = sys_console(buffer, (unsigned)n);
        if (error != n) {
            if (error >= 0)
                error = -5;
            goto failed;
        }
    }
    error = sys_socket_close(fd);
    fd = -1;
    if (error)
        goto failed;
    printf("HTTP bytes/hash: %08x %08x\n\n[user] HTTP download complete\n", bytes, hash);
    return 0;
failed:
    if (fd >= 0)
        sys_socket_close(fd);
    printf("[user] HTTP failed errno=%d (check dhcp and server)\n", -error);
    return 1;
}
