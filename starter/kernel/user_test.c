#include "process.h"
#include "user_vm.h"
#include "bytes.h"
static struct ramfs filesystem;
#define ELF(n) extern const unsigned char user_elf_##n[], user_elf_##n##_end[];
ELF(0) ELF(1) ELF(2) ELF(3) ELF(4) ELF(5) ELF(6) ELF(7) ELF(8) ELF(9) ELF(10) ELF(11) ELF(12)
struct ramfs *user_filesystem(void) { return &filesystem; }
static const unsigned char *const images[] = {user_elf_0,user_elf_1,user_elf_2,user_elf_3,user_elf_4,user_elf_5,user_elf_6,user_elf_7,user_elf_8,user_elf_9,user_elf_10,user_elf_11,user_elf_12};
static const unsigned char *const ends[] = {user_elf_0_end,user_elf_1_end,user_elf_2_end,user_elf_3_end,user_elf_4_end,user_elf_5_end,user_elf_6_end,user_elf_7_end,user_elf_8_end,user_elf_9_end,user_elf_10_end,user_elf_11_end,user_elf_12_end};
int64_t user_demo_spawn(unsigned index, uint64_t *pid) {
    if (index >= 13) return SYS_ENOENT;
    return user_spawn(images[index], (size_t)(ends[index] - images[index]), index == 6 ? NULL : &filesystem, index == 6 ? 0 : CAP_ALL, pid);
}
int64_t user_demo_run(unsigned index) {
    if (index >= 13) return SYS_ENOENT;
    return user_run(images[index], (size_t)(ends[index] - images[index]), index == 6 ? NULL : &filesystem, index == 6 ? 0 : CAP_ALL);
}
int user_selftest(void) {
    fs_init(&filesystem);
    uint64_t before;
    /* Every launch must release its private page tables, user frames and handles. */
    int64_t result = user_run(user_elf_0, (size_t)(user_elf_0_end - user_elf_0), &filesystem, CAP_ALL);
    if (result) return (int)result + 100;
    before = pmm_free_count();
#define RUN(n, expected) do { \
    result = user_run(user_elf_##n, (size_t)(user_elf_##n##_end - user_elf_##n), &filesystem, CAP_ALL); \
    if (result != (expected) || pmm_free_count() != before) return (n) + 200; \
} while (0)
    RUN(0, 0); RUN(1, -1014); RUN(2, -1013); RUN(3, -2000); RUN(4, -1014); RUN(5, -1013);
    RUN(7, 0); RUN(8, -1014);
    if (!bytes_equal(filesystem.image.nodes[0].data, "commit", 6)) return 220;
    result = user_run(user_elf_6, (size_t)(user_elf_6_end - user_elf_6), NULL, 0);
    if (result || pmm_free_count() != before || filesystem.root.refs != 1) return 221;
    RUN(12, 0);
    return 0;
}

#include "scheduler.h"
#include "pipe.h"
static handle_t task_pipe_read, task_pipe_write;
static unsigned pipe_seen, pipe_failed;
static void pipe_writer(void *unused) {
    (void)unused;
    const unsigned char message[] = {1,0,3,0,1,0,0,0,1};
    if (pipe_send(task_handle_table(), task_pipe_write, message, sizeof(message))) pipe_failed = 1;
}
static void pipe_reader(void *unused) {
    (void)unused;
    unsigned char message[9]; size_t length;
    for (;;) {
        int64_t result = pipe_receive(task_handle_table(), task_pipe_read, message, sizeof(message), 3, &length);
        if (result == SYS_EAGAIN && !pipe_failed) { task_yield(); continue; }
        if (result || length != 9 || message[8] != 1) pipe_failed = 1;
        else pipe_seen++;
        return;
    }
}
int pipe_task_selftest(void) {
    handle_t read, write;
    if (pipe_create(task_handle_table(), &read, &write)) return 0;
    struct task_grant read_grant = {read, CAP_READ}, write_grant = {write, CAP_WRITE};
    if (task_create_with_caps(pipe_reader, NULL, &read_grant, 1, &task_pipe_read) < 0 ||
        task_create_with_caps(pipe_writer, NULL, &write_grant, 1, &task_pipe_write) < 0) return 0;
    if (handle_close(task_handle_table(), read) || handle_close(task_handle_table(), write)) return 0;
    uint64_t start = timer_now();
    while (task_live_count()) {
        task_yield();
        if (timer_now() - start > 200) return 0;
    }
    task_yield();
    return pipe_seen == 1 && !pipe_failed;
}
