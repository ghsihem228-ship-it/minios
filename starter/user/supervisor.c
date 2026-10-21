#include "mini.h"
extern const unsigned char ipc_elf[], ipc_elf_end[];
#define CHECK(x, n) do { if (!(x)) return n; } while (0)
static struct syscall_result call(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return syscall_invoke(n, a, b, c, d, 0, 0);
}
static struct syscall_result spawn(const struct launch_grant *grants, size_t count) {
    return call(SYS_PROCESS_SPAWN, (uintptr_t)ipc_elf, (size_t)(ipc_elf_end-ipc_elf), (uintptr_t)grants, count);
}
int user_main(uint64_t root) {
    mini_init(root);
    uint64_t requests[2], replies[2];
    CHECK(call(SYS_ABI_QUERY, 1, 0, 0, 0).value == SYS_ABI_VERSION, 1);
    CHECK(!call(SYS_PIPE_CREATE, (uintptr_t)requests, 0, 0, 0).status, 2);
    CHECK(!call(SYS_PIPE_CREATE, (uintptr_t)replies, 0, 0, 0).status, 3);
    CHECK(call(SYS_OBJECT_WAIT, requests[0], SIGNAL_READABLE, 0, 0).status == SYS_EAGAIN, 4);
    CHECK(call(SYS_OBJECT_WAIT, requests[0], SIGNAL_READABLE, 2, 0).status == SYS_ETIMEDOUT, 5);
    CHECK(call(SYS_OBJECT_WAIT, requests[0], SIGNAL_WRITABLE, 0, 0).status == SYS_EINVAL, 6);
    CHECK(call(SYS_SLEEP, UINT64_MAX, 0, 0, 0).status == SYS_EINVAL, 7);
    CHECK(!call(SYS_YIELD, 0, 0, 0, 0).status, 8);
    struct wait_item items[8];
    uint64_t ready;
    for (unsigned i = 0; i < 8; i++) items[i] = (struct wait_item){requests[1], SIGNAL_WRITABLE};
    CHECK(!mini_wait_many(items, 8, 0, &ready) && ready == 255, 101);
    items[1] = (struct wait_item){requests[0], SIGNAL_READABLE};
    CHECK(!mini_wait_many(items, 2, 0, &ready) && ready == 1, 102);
    items[0] = items[1];
    CHECK(call(SYS_WAIT_MANY, (uintptr_t)items, 2, 0, 0).status == SYS_EAGAIN, 103);
    CHECK(call(SYS_WAIT_MANY, (uintptr_t)items, 2, 2, 0).status == SYS_ETIMEDOUT, 104);
    CHECK(call(SYS_WAIT_MANY, 0x100000, 2, 0, 0).status == SYS_EFAULT, 105);
    CHECK(call(SYS_WAIT_MANY, (uintptr_t)items, 0, 0, 0).status == SYS_EINVAL, 106);
    CHECK(call(SYS_WAIT_MANY, (uintptr_t)items, 9, 0, 0).status == SYS_EINVAL, 107);
    CHECK(call(SYS_WAIT_MANY, (uintptr_t)items, 2, UINT64_MAX-1, 0).status == SYS_EINVAL, 108);
    items[0] = (struct wait_item){requests[1], SIGNAL_WRITABLE};
    items[1].signals = SIGNAL_EXITED;
    CHECK(call(SYS_WAIT_MANY, (uintptr_t)items, 2, 0, 0).status == SYS_EINVAL, 109);
    items[1] = (struct wait_item){0, SIGNAL_READABLE};
    CHECK(call(SYS_WAIT_MANY, (uintptr_t)items, 2, 0, 0).status == SYS_EBADHANDLE, 110);
    struct syscall_result denied = call(SYS_HANDLE_CLONE, requests[0], CAP_READ, 0, 0);
    CHECK(!denied.status, 111);
    items[1] = (struct wait_item){denied.value, SIGNAL_READABLE};
    CHECK(call(SYS_WAIT_MANY, (uintptr_t)items, 2, 0, 0).status == SYS_EACCESS, 112);
    CHECK(!call(SYS_HANDLE_CLOSE, denied.value, 0, 0, 0).status, 113);
    struct launch_grant grants[2] = {{requests[0], CAP_READ | CAP_WAIT}, {replies[1], CAP_WRITE | CAP_WAIT}};
    CHECK(call(SYS_PROCESS_SPAWN, 0x100000, 64, 0, 0).status == SYS_EFAULT, 9);
    CHECK(call(SYS_PROCESS_SPAWN, (uintptr_t)ipc_elf, 3, 0, 0).status == SYS_EFORMAT, 10);
    CHECK(call(SYS_PROCESS_SPAWN, (uintptr_t)ipc_elf, (size_t)(ipc_elf_end-ipc_elf), 0x100000, 1).status == SYS_EFAULT, 11);
    struct launch_grant invalid[2] = {grants[0], {replies[1], CAP_ALL}};
    CHECK(spawn(invalid, 2).status == SYS_EACCESS, 12); /* partial grant rollback */
    struct syscall_result r = call(SYS_HANDLE_CLONE, requests[0], CAP_READ | CAP_WAIT, 0, 0);
    CHECK(!r.status, 13);
    uint64_t restricted = r.value;
    invalid[0] = (struct launch_grant){restricted, CAP_READ}; invalid[1] = grants[1];
    CHECK(spawn(invalid, 2).status == SYS_EACCESS, 14); /* no TRANSFER authority */
    CHECK(!call(SYS_HANDLE_CLOSE, restricted, 0, 0, 0).status, 15);
    /* Fill the parent table so publishing its process capability must roll back. */
    uint64_t copies[32]; unsigned used = 0;
    while (!(r = call(SYS_HANDLE_DUPLICATE, requests[0], 0, 0, 0)).status) copies[used++] = r.value;
    CHECK(r.status == SYS_ENOSPC && used < 32, 16);
    CHECK(spawn(grants, 2).status == SYS_ENOSPC, 17);
    while (used) CHECK(!call(SYS_HANDLE_CLOSE, copies[--used], 0, 0, 0).status, 18);
    r = spawn(grants, 2); CHECK(!r.status, 19);
    uint64_t child = r.value;
    CHECK(call(SYS_PROCESS_STATUS, child, 0, 0, 0).status == SYS_EAGAIN, 20);
    r = call(SYS_HANDLE_CLONE, child, CAP_WAIT, 0, 0); CHECK(!r.status, 21);
    uint64_t waiter = r.value;
    CHECK(call(SYS_PROCESS_KILL, waiter, 0, 0, 0).status == SYS_EACCESS, 22);
    CHECK(call(SYS_PROCESS_STATUS, requests[0], 0, 0, 0).status == SYS_EBADHANDLE, 23);
    CHECK(!call(SYS_HANDLE_CLOSE, requests[0], 0, 0, 0).status, 24);
    CHECK(!call(SYS_HANDLE_CLOSE, replies[1], 0, 0, 0).status, 25);
    unsigned char message[16] = {1,0,1,0,8,0,0,0};
    for (unsigned i = 0; i < 32; i++) {
        message[8] = (unsigned char)i;
        CHECK(!call(SYS_OBJECT_WAIT, requests[1], SIGNAL_WRITABLE, 200, 0).status, 26);
        CHECK(!call(SYS_PIPE_SEND, requests[1], (uintptr_t)message, sizeof(message), 0).status, 27);
        items[0] = (struct wait_item){replies[0], SIGNAL_READABLE};
        items[1] = (struct wait_item){child, SIGNAL_EXITED};
        CHECK(!mini_wait_many(items, 2, 200, &ready) && ready == 1, 28);
        r = call(SYS_PIPE_RECEIVE, replies[0], (uintptr_t)message, sizeof(message), 1);
        CHECK(!r.status && r.value == 16 && message[8] == i+1, 29);
    }
    CHECK(!call(SYS_HANDLE_CLOSE, requests[1], 0, 0, 0).status, 30);
    r = call(SYS_OBJECT_WAIT, waiter, SIGNAL_EXITED, 200, 0);
    CHECK(!r.status && r.value == SIGNAL_EXITED, 31);
    items[0] = (struct wait_item){replies[0], SIGNAL_READABLE | SIGNAL_PEER_CLOSED};
    items[1] = (struct wait_item){waiter, SIGNAL_EXITED};
    CHECK(!mini_wait_many(items, 2, 0, &ready) && ready == 3, 114);
    r = call(SYS_PROCESS_STATUS, waiter, 0, 0, 0); CHECK(!r.status && !r.value, 32);
    r = call(SYS_OBJECT_WAIT, replies[0], SIGNAL_READABLE | SIGNAL_PEER_CLOSED, 0, 0);
    CHECK(!r.status && r.value == (SIGNAL_READABLE | SIGNAL_PEER_CLOSED), 33);
    r = call(SYS_PIPE_RECEIVE, replies[0], (uintptr_t)message, sizeof(message), 1);
    CHECK(!r.status && !r.value, 34);
    CHECK(!call(SYS_HANDLE_CLOSE, child, 0, 0, 0).status, 35);
    CHECK(!call(SYS_PROCESS_STATUS, waiter, 0, 0, 0).status, 36); /* pinned exit survives close */
    CHECK(!call(SYS_HANDLE_CLOSE, waiter, 0, 0, 0).status, 37);
    CHECK(call(SYS_PROCESS_STATUS, child, 0, 0, 0).status == SYS_EBADHANDLE, 38);
    CHECK(!call(SYS_HANDLE_CLOSE, replies[0], 0, 0, 0).status, 39);
    /* A second child is killed while sleeping; its endpoints close at termination. */
    CHECK(!call(SYS_PIPE_CREATE, (uintptr_t)requests, 0, 0, 0).status, 40);
    CHECK(!call(SYS_PIPE_CREATE, (uintptr_t)replies, 0, 0, 0).status, 41);
    grants[0] = (struct launch_grant){requests[0], CAP_READ | CAP_WAIT};
    grants[1] = (struct launch_grant){replies[1], CAP_WRITE | CAP_WAIT};
    r = spawn(grants, 2); CHECK(!r.status, 42); child = r.value;
    for (unsigned i = 8; i < 16; i++) message[i] = 255;
    CHECK(!call(SYS_PIPE_SEND, requests[1], (uintptr_t)message, sizeof(message), 0).status, 43);
    CHECK(!call(SYS_SLEEP, 10, 0, 0, 0).status, 44);
    CHECK(!call(SYS_PROCESS_KILL, child, 0, 0, 0).status, 45);
    r = call(SYS_PROCESS_STATUS, child, 0, 0, 0);
    CHECK(!r.status && (int64_t)r.value == -2001, 46);
    CHECK(!call(SYS_HANDLE_CLOSE, requests[0], 0, 0, 0).status, 47);
    CHECK(call(SYS_PIPE_SEND, requests[1], (uintptr_t)message, sizeof(message), 0).status == SYS_EPIPE, 48);
    CHECK(!call(SYS_HANDLE_CLOSE, child, 0, 0, 0).status, 49);
    CHECK(!call(SYS_HANDLE_CLOSE, requests[1], 0, 0, 0).status, 50);
    CHECK(!call(SYS_HANDLE_CLOSE, replies[0], 0, 0, 0).status, 51);
    CHECK(!call(SYS_HANDLE_CLOSE, replies[1], 0, 0, 0).status, 52);
    /* Dropping the last process handle does not kill a running child. It must
     * still consume EOF and release the scheduler's last reference on exit. */
    CHECK(!call(SYS_PIPE_CREATE, (uintptr_t)requests, 0, 0, 0).status, 53);
    CHECK(!call(SYS_PIPE_CREATE, (uintptr_t)replies, 0, 0, 0).status, 54);
    grants[0] = (struct launch_grant){requests[0], CAP_READ | CAP_WAIT};
    grants[1] = (struct launch_grant){replies[1], CAP_WRITE | CAP_WAIT};
    CHECK(!mini_spawn(ipc_elf, (size_t)(ipc_elf_end-ipc_elf), grants, 2, &child), 55);
    CHECK(!call(SYS_HANDLE_CLOSE, child, 0, 0, 0).status, 56);
    CHECK(!call(SYS_HANDLE_CLOSE, requests[0], 0, 0, 0).status, 57);
    CHECK(!call(SYS_HANDLE_CLOSE, requests[1], 0, 0, 0).status, 58);
    CHECK(!call(SYS_HANDLE_CLOSE, replies[1], 0, 0, 0).status, 59);
    uint64_t observed;
    CHECK(!mini_wait(replies[0], SIGNAL_READABLE | SIGNAL_PEER_CLOSED, 200, &observed) && observed == 10, 60);
    CHECK(!call(SYS_HANDLE_CLOSE, replies[0], 0, 0, 0).status, 61);
    /* Kill while holding a blocked wait's endpoint pin; that pin must release. */
    CHECK(!call(SYS_PIPE_CREATE, (uintptr_t)requests, 0, 0, 0).status, 62);
    CHECK(!call(SYS_PIPE_CREATE, (uintptr_t)replies, 0, 0, 0).status, 63);
    grants[0] = (struct launch_grant){requests[0], CAP_READ | CAP_WAIT};
    grants[1] = (struct launch_grant){replies[1], CAP_WRITE | CAP_WAIT};
    CHECK(!mini_spawn(ipc_elf, (size_t)(ipc_elf_end-ipc_elf), grants, 2, &child), 64);
    CHECK(!mini_sleep(10) && !mini_kill(child), 65);
    int64_t exit_code;
    CHECK(!mini_process_status(child, &exit_code) && exit_code == -2001, 66);
    CHECK(!call(SYS_HANDLE_CLOSE, requests[0], 0, 0, 0).status, 67);
    CHECK(call(SYS_PIPE_SEND, requests[1], (uintptr_t)message, sizeof(message), 0).status == SYS_EPIPE, 68);
    CHECK(!call(SYS_HANDLE_CLOSE, child, 0, 0, 0).status, 69);
    /* Remaining endpoints intentionally left open: parent exit must close them. */
    return 0;
}
