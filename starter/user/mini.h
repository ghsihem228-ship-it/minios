#ifndef MINI_H
#define MINI_H
#include "syscall_abi.h"
#include <stddef.h>
typedef long ssize_t;
typedef long off_t;
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define SEEK_SET 0
extern int errno;
void mini_init(uint64_t directory);
int open(const char *, int flags, ...);
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
off_t lseek(int, off_t, int);
int close(int);
uint64_t mini_handle(int);
int mini_spawn(const void *, size_t, const struct launch_grant *, size_t, uint64_t *process);
int mini_process_status(uint64_t process, int64_t *code);
int mini_kill(uint64_t process);
int mini_wait(uint64_t handle, uint64_t signals, uint64_t ticks, uint64_t *observed);
int mini_wait_many(const struct wait_item *, size_t, uint64_t ticks, uint64_t *ready);
int mini_sleep(uint64_t ticks);
int mini_yield(void);
size_t mini_error(char *, size_t);
#endif
