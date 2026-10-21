#include "mini.h"
#define CHECK(x, n) do { if (!(x)) return n; } while (0)
static struct syscall_result call(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return syscall_invoke(n, a, b, c, d, 0, 0);
}
int user_main(uint64_t root, const uint64_t *grants, uint64_t count) {
    CHECK(!root && count == 2, 81);
    mini_init(root);
    CHECK(open("demo", O_RDONLY) == -1 && errno == 9, 82);
    unsigned char message[16] = {1,0,1,0,8,0,0,0};
    CHECK(call(SYS_PIPE_SEND, grants[0], (uintptr_t)message, sizeof(message), 0).status == SYS_EACCESS, 83);
    CHECK(call(SYS_HANDLE_DUPLICATE, grants[0], 0, 0, 0).status == SYS_EACCESS, 84);
    CHECK(call(SYS_SLEEP, 3, 0, 0, 0).status == 0, 85);
    for (;;) {
        struct syscall_result r = call(SYS_OBJECT_WAIT, grants[0], SIGNAL_READABLE, 200, 0);
        CHECK(!r.status && (r.value & SIGNAL_READABLE), 86);
        r = call(SYS_PIPE_RECEIVE, grants[0], (uintptr_t)message, sizeof(message), 1);
        CHECK(!r.status, 87);
        if (!r.value) break;
        CHECK(r.value == sizeof(message), 88);
        uint64_t value = 0;
        for (unsigned i = 0; i < 8; i++) value |= (uint64_t)message[8+i] << (8*i);
        if (value == UINT64_MAX) { /* Deliberate blocked child, used for kill tests. */
            CHECK(!call(SYS_SLEEP, 1000, 0, 0, 0).status, 89);
        }
        value++;
        for (unsigned i = 0; i < 8; i++) message[8+i] = (unsigned char)(value >> (8*i));
        r = call(SYS_OBJECT_WAIT, grants[1], SIGNAL_WRITABLE | SIGNAL_PEER_CLOSED, 200, 0);
        CHECK(!r.status, 90);
        if (r.value & SIGNAL_PEER_CLOSED) return 0;
        CHECK(!call(SYS_PIPE_SEND, grants[1], (uintptr_t)message, sizeof(message), 0).status, 91);
    }
    return 0; /* Exit closes granted endpoints, making EOF visible to the parent. */
}
