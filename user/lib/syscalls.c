#include "minios.h"
_Static_assert(sizeof(void *) == 4, "i386 syscall ABI");
static int call(unsigned number, uint32_t a, uint32_t b, uint32_t c) {
    int result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c)
                     : "memory", "cc");
    return result;
}
int sys_console(const void *p, size_t n) { return call(1, (uintptr_t)p, n, 0); }
void sys_exit(int code) {
    call(2, (unsigned)code, 0, 0);
    for (;;)
        __asm__ volatile("ud2");
}
int sys_yield(void) { return call(3, 0, 0, 0); }
int sys_getpid(void) { return call(4, 0, 0, 0); }
uint32_t sys_ticks(void) { return (uint32_t)call(5, 0, 0, 0); }
int sys_sleep(uint32_t n) { return call(6, n, 0, 0); }
int sys_open(const char *p, unsigned flags) { return call(7, (uintptr_t)p, flags, 0); }
int sys_read(int fd, void *p, size_t n) { return call(8, fd, (uintptr_t)p, n); }
int sys_close(int fd) { return call(9, fd, 0, 0); }
int sys_write(int fd, const void *p, size_t n) { return call(10, fd, (uintptr_t)p, n); }
int sys_seek(int fd, uint32_t off) { return call(11, fd, off, 0); }
int sys_unlink(const char *p) { return call(12, (uintptr_t)p, 0, 0); }
int sys_sync(void) { return call(13, 0, 0, 0); }
int sys_stat(const char *p, struct mini_stat *s) { return call(14, (uintptr_t)p, (uintptr_t)s, 0); }
int sys_socket(unsigned type) { return call(15, type, 0, 0); }
int sys_connect(int fd, uint32_t ip, unsigned port) { return call(16, fd, ip, port); }
int sys_send(int fd, const void *p, size_t n) { return call(17, fd, (uintptr_t)p, n); }
int sys_recv(int fd, void *p, size_t n) { return call(18, fd, (uintptr_t)p, n); }
int sys_socket_close(int fd) { return call(19, fd, 0, 0); }
int sys_socket_status(int fd) { return call(20, fd, 0, 0); }
int sys_bind(int fd, unsigned port) { return call(21, fd, port, 0); }
int write_all(int fd, const void *data, size_t len) {
    if (len > 0x7fffffffu)
        return -7;
    const uint8_t *p = data;
    size_t done = 0;
    while (done < len) {
        size_t n = len - done;
        if (n > 1024)
            n = 1024;
        int wrote = sys_write(fd, p + done, n);
        if (wrote <= 0)
            return wrote ? wrote : -5;
        done += (unsigned)wrote;
    }
    return (int)done;
}
