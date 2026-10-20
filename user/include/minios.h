#ifndef MINIOS_USER_H
#define MINIOS_USER_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
/* Freestanding i386 API. All syscall wrappers return negative errno directly. */
#define FS_WRITE 1u
#define FS_CREATE 2u
#define FS_TRUNC 4u
#define FS_EXCL 8u
#define SOCK_UDP 1u
#define SOCK_TCP 2u
#define SOCK_CONNECTED 1u
#define SOCK_READABLE 2u
#define SOCK_WRITABLE 4u
#define SOCK_EOF 8u
#define SOCK_CONNECTING 16u
#define EAGAIN 11
#define EINPROGRESS 115
struct mini_stat {
    uint32_t size;
    int readonly;
};
int sys_console(const void *data, size_t len);
__attribute__((noreturn)) void sys_exit(int code);
int sys_yield(void);
int sys_getpid(void);
uint32_t sys_ticks(void);
int sys_sleep(uint32_t ticks);
int sys_open(const char *path, unsigned flags);
int sys_read(int fd, void *data, size_t len);
int sys_write(int fd, const void *data, size_t len);
int sys_close(int fd);
int sys_seek(int fd, uint32_t offset);
int sys_unlink(const char *path);
int sys_sync(void);
int sys_stat(const char *path, struct mini_stat *out);
int sys_socket(unsigned type);
int sys_connect(int fd, uint32_t address, unsigned port);
int sys_send(int fd, const void *data, size_t len);
int sys_recv(int fd, void *data, size_t len);
int sys_socket_close(int fd);
int sys_socket_status(int fd);
int sys_bind(int fd, unsigned port);
int write_all(int fd, const void *data, size_t len);
size_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t len);
void *memcpy(void *out, const void *in, size_t len);
void *memmove(void *out, const void *in, size_t len);
void *memset(void *out, int byte, size_t len);
int memcmp(const void *a, const void *b, size_t len);
int parse_u32(const char *text, uint32_t *out);
int parse_ipv4(const char *text, uint32_t *out);
int snprintf(char *out, size_t cap, const char *format, ...);
int vsnprintf(char *out, size_t cap, const char *format, va_list args);
int printf(const char *format, ...);
#endif
