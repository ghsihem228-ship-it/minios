#include "mini.h"
#ifndef USER_TEST
#define USER_TEST 0
#endif
static int equal(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}
#define CHECK(c, n) do { if (!(c)) return (n); } while (0)
int user_main(uint64_t root) {
    if (USER_TEST == 1) { *(volatile uint64_t *)(uintptr_t)0x100000 = 7; return 90; }
    if (USER_TEST == 2) { __asm__ volatile ("outb %%al, $0x80" :: "a"(0)); return 91; }
    if (USER_TEST == 3) { for (;;) __asm__ volatile ("pause"); }
    if (USER_TEST == 4) { volatile unsigned char code[1] = {0xc3}; ((void (*)(void))(uintptr_t)code)(); return 92; }
    if (USER_TEST == 5) { __asm__ volatile ("int $0x20"); return 93; }
    mini_init(root);
    if (USER_TEST == 6) {
        int missing = open("demo", O_RDONLY);
        return missing == -1 && errno == 9 ? 0 : 94;
    }
    if (USER_TEST == 7) {
        int opened = open("demo", O_RDWR);
        CHECK(opened >= 0, 95);
        struct syscall_result tx = syscall_invoke(SYS_TX_BEGIN, root, 0, 0, 0, 0, 0);
        CHECK(!tx.status, 96);
        CHECK(write(opened, "aborted", 7) == 7, 97);
        return 0; /* pending transaction must abort on exit */
    }
    if (USER_TEST == 8) { *(volatile uint64_t *)(uintptr_t)0x40000000 = 42; return 98; }

    int fd = open("demo", O_RDWR | O_CREAT);
    CHECK(fd >= 0, 1);
    CHECK(write(fd, "before", 6) == 6 && lseek(fd, 0, SEEK_SET) == 0, 2);
    char data[64];
    CHECK(read(fd, data, 6) == 6 && equal(data, "before", 6), 3);
    CHECK(lseek(fd, 0, SEEK_SET) == 0, 4);
    struct syscall_result r = syscall_invoke(SYS_FS_READ, mini_handle(fd), 0x100000, 1, 0, 0, 0);
    CHECK(r.status == SYS_EFAULT, 5);
    r = syscall_invoke(SYS_FS_READ, mini_handle(fd), 0x40000000, 1, 0, 0, 0);
    CHECK(r.status == SYS_EFAULT, 5);
    r = syscall_invoke(SYS_FS_READ, mini_handle(fd), 0x4000ffff, 2, 0, 0, 0);
    CHECK(r.status == SYS_EFAULT, 5);
    CHECK(read(fd, data, 6) == 6 && equal(data, "before", 6), 6);
    int reader = open("demo", O_RDONLY);
    CHECK(reader >= 0, 7);
    CHECK(write(reader, "x", 1) == -1 && errno == 13, 8);
    CHECK(mini_error(data, sizeof(data)) > 0 && equal(data, "write failed", 12), 9);
    r = syscall_invoke(SYS_ERROR_GET, 2, 2, 0, 0, 0, 0);
    CHECK(!r.status && r.value == CAP_WRITE, 10);
    CHECK(close(reader) == 0 && lseek(fd, 0, SEEK_SET) == 0, 11);
    volatile uint64_t marker = 7;
    r = syscall_invoke(SYS_TX_BEGIN, root, 0, 0, 0, 0, 0);
    CHECK(!r.status, 12);
    if (!r.value) {
        marker = 9;
        CHECK(write(fd, "after!", 6) == 6, 13);
        r = syscall_invoke(SYS_TX_ROLLBACK, 0, 0, 0, 0, 0, 0);
        return 14; /* rollback resumes the BEGIN call, never this line */
    }
    CHECK(marker == 7 && read(fd, data, 6) == 6 && equal(data, "before", 6), 15);
    CHECK(lseek(fd, 0, SEEK_SET) == 0, 16);
    r = syscall_invoke(SYS_TX_BEGIN, root, 0, 0, 0, 0, 0);
    CHECK(!r.status && !r.value, 17);
    marker = 10;
    CHECK(write(fd, "commit", 6) == 6, 18);
    r = syscall_invoke(SYS_TX_COMMIT, 0, 0, 0, 0, 0, 0);
    CHECK(!r.status && marker == 10, 19);
    CHECK(lseek(fd, 0, SEEK_SET) == 0 && read(fd, data, 6) == 6 && equal(data, "commit", 6), 20);
    CHECK(close(fd) == 0, 21);
    uint64_t ends[2];
    r = syscall_invoke(SYS_PIPE_CREATE, (uintptr_t)ends, 0, 0, 0, 0, 0);
    CHECK(!r.status, 22);
    const unsigned char message[] = {1,0,6,0,25,0,0,0, 1,0,1,0,8,0,0,0,42,0,0,0,0,0,0,0, 2,0,3,0,1,0,0,0,1};
    r = syscall_invoke(SYS_PIPE_SEND, ends[1], (uintptr_t)message, sizeof(message), 0, 0, 0);
    CHECK(!r.status, 23);
    r = syscall_invoke(SYS_PIPE_RECEIVE, ends[0], (uintptr_t)data, sizeof(data), 4, 0, 0);
    CHECK(r.status == SYS_EFORMAT, 24);
    r = syscall_invoke(SYS_PIPE_RECEIVE, ends[0], (uintptr_t)data, sizeof(data), 6, 0, 0);
    CHECK(!r.status && r.value == sizeof(message) && equal(data, message, sizeof(message)), 25);
    r = syscall_invoke(SYS_HANDLE_CLOSE, ends[0], 0, 0, 0, 0, 0); CHECK(!r.status, 26);
    r = syscall_invoke(SYS_HANDLE_CLOSE, ends[1], 0, 0, 0, 0, 0); CHECK(!r.status, 27);
    return 0;
}
