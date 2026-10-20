/* Checks execute through the real C startup and int 0x80 ABI in Ring 3. */
#include "minios.h"
#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            printf("[user] C runtime check failed line=%u\n", (unsigned)__LINE__);                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static volatile uint32_t zeroed;
static volatile int initialized = 12345;
int main(int argc, char **argv) {
    CHECK(argc >= 1 && argv[0] && argv[argc] == 0 && argv[argc + 1] == 0);
    CHECK(((uintptr_t)argv & 15u) == 4u);
    CHECK(*(const uint32_t *)((uintptr_t)argv - 4) == (unsigned)argc);
    CHECK(!zeroed && initialized == 12345);
    char buffer[128], small[5];
    CHECK(snprintf(buffer, sizeof(buffer), "%d %u %08x %X %c %s %%", (int)0x80000000u, UINT32_MAX,
                   0x123u, 0xabcdu, 'Q', "hello") == 46);
    CHECK(!strcmp(buffer, "-2147483648 4294967295 00000123 ABCD Q hello %"));
    CHECK(snprintf(small, sizeof(small), "abcdef") == 6 && !strcmp(small, "abcd"));
    CHECK(snprintf(small, 1, "abc") == 3 && !small[0]);
    CHECK(snprintf(0, 0, "%u", 12345u) == 5);
    CHECK(snprintf(buffer, sizeof(buffer), "%06d", -12) == 6 && !strcmp(buffer, "-00012"));
    CHECK(snprintf(buffer, sizeof(buffer), "%5s", "x") == 5 && !strcmp(buffer, "    x"));
    CHECK(snprintf(buffer, sizeof(buffer), "%p", (void *)0x1234) == 10 &&
          !strcmp(buffer, "0x00001234"));
    CHECK(snprintf(buffer, sizeof(buffer), "%f", 1) < 0);
    CHECK(snprintf(buffer, sizeof(buffer), "%99u", 1u) < 0);
    memset(buffer, 'x', sizeof(buffer));
    memcpy(buffer, "abcdef", 7);
    memmove(buffer + 1, buffer, 6);
    CHECK(!memcmp(buffer, "aabcdef", 7));
    memmove(buffer, buffer + 1, 6);
    CHECK(!memcmp(buffer, "abcdef", 6));
    CHECK(!strncmp("hello", "help", 3) && strcmp("a", "b") < 0);
    uint32_t value;
    CHECK(parse_u32("4294967295", &value) && value == UINT32_MAX);
    CHECK(!parse_u32("4294967296", &value) && !parse_u32("-1", &value) && !parse_u32("", &value));
    CHECK(parse_ipv4("10.0.2.2", &value) && value == 0x0a000202u);
    CHECK(!parse_ipv4("10.0.2.256", &value) && !parse_ipv4("10.0.2", &value));
    CHECK(sys_getpid() > 0 && sys_yield() == 0 && sys_sleep(1) == 0);
    CHECK(sys_read(99, buffer, 1) == -9 && sys_console((void *)0x100000, 1) == -14);
    int fd = sys_open("/tmp/c-test", FS_WRITE | FS_CREATE | FS_EXCL);
    CHECK(fd >= 0);
    CHECK(write_all(fd, "Hello from C!", 13) == 13 && sys_seek(fd, 0) == 0);
    memset(buffer, 0, sizeof(buffer));
    CHECK(sys_read(fd, buffer, sizeof(buffer)) == 13 && !memcmp(buffer, "Hello from C!", 13));
    struct mini_stat stat;
    CHECK(sys_stat("/tmp/c-test", &stat) == 0 && stat.size == 13 && !stat.readonly);
    char bulk[1500];
    memset(bulk, 'A', sizeof(bulk));
    CHECK(sys_seek(fd, 0) == 0 && write_all(fd, bulk, sizeof(bulk)) == (int)sizeof(bulk));
    CHECK(sys_read(fd, bulk, 1025) == -7);
    CHECK(sys_seek(fd, 1499) == 1499 && sys_read(fd, buffer, 1) == 1 && buffer[0] == 'A');
    CHECK(sys_stat("/tmp/c-test", &stat) == 0 && stat.size == 1500);
    CHECK(sys_close(fd) == 0 && sys_unlink("/tmp/c-test") == 0);
    int sync = sys_sync();
    CHECK(sync == 0 || sync == -19);
    fd = sys_socket(SOCK_UDP);
    CHECK(fd >= 0 && sys_socket_status(fd) == 0);
    CHECK(sys_bind(fd, 20001) == 0 && sys_socket_close(fd) == 0 && sys_socket_close(fd) == -9);
    /* Argument strings are mutable and belong to this process. */
    char original = argv[0][0];
    argv[0][0] = '!';
    CHECK(argv[0][0] == '!');
    argv[0][0] = original;
    zeroed = 99;
    initialized = 0;
    printf("[user] C runtime passed argc=%d\n", argc);
    return 0;
}
