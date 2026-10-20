#include "console.h"
#include "serial.h"
#include "process.h"
#include "scheduler.h"
#include "pmm.h"
#include "store.h"
#include "bytes.h"
#define LINE 128u
static struct handle_table files;
static handle_t root;
static struct ramfs *filesystem;
static void say(const char *s) { (void)serial_write(s); }
static int same(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static size_t length(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void number(uint64_t value) {
    char buffer[21]; unsigned at = sizeof(buffer) - 1; buffer[at] = 0;
    do { buffer[--at] = (char)('0' + value % 10); value /= 10; } while (value);
    say(buffer + at);
}
static void result(int64_t status) {
    if (!status) return;
    say("Error "); if (status < 0) say("-");
    number(status < 0 ? UINT64_C(0) - (uint64_t)status : (uint64_t)status);
    say(status == SYS_ENOENT ? ": file not found\n" : status == SYS_EINVAL ? ": invalid name or arguments\n" :
        status == SYS_ENOSPC ? ": capacity exhausted\n" : status == SYS_EBUSY ? ": file is busy\n" :
        status == SYS_EIO ? ": disk I/O failed; further saves disabled\n" :
        status == SYS_EACCESS ? ": permission denied\n" : ": operation failed\n");
}
static char *word(char **rest) {
    char *s = *rest; while (*s == ' ') s++;
    if (!*s) { *rest = s; return NULL; }
    char *start = s; while (*s && *s != ' ') s++;
    if (*s) *s++ = 0;
    while (*s == ' ') s++;
    *rest = s; return start;
}
static void execute(char *line) {
    char *rest = line, *command = word(&rest);
    if (!command) return;
    char *name = word(&rest);
    if (same(command, "write")) {
        if (!name) { say("Usage: write <name> <text>\n"); return; }
        handle_t file; size_t done;
        int64_t status = fs_open(&files, root, name, length(name), CAP_READ | CAP_WRITE, 1, &file);
        if (!status) {
            /* Existing files are overwritten, not implicitly truncated. */
            status = fs_write(&files, file, rest, length(rest), &done);
            (void)handle_close(&files, file);
            if (!status) { say("Wrote bytes="); number(done); say("\n"); }
        }
        result(status); return;
    }
    if (same(command, "cat") || same(command, "rm")) {
        if (!name || *rest) { say("Usage: cat <name> | rm <name>\n"); return; }
        if (same(command, "rm")) {
            int64_t status = fs_unlink(&files, root, name, length(name)); result(status);
            if (!status) say("Removed file\n");
        } else {
            handle_t file; unsigned char data[FS_BYTES]; size_t done;
            int64_t status = fs_open(&files, root, name, length(name), CAP_READ, 0, &file);
            if (!status) {
                status = fs_read(&files, file, data, sizeof(data), &done);
                (void)handle_close(&files, file);
                if (!status) {
                    for (size_t i = 0; i < done; i++) {
                        /* Do not emit terminal control/escape sequences from files. */
                        char text[2] = {data[i] == '\n' || (data[i] >= 32 && data[i] < 127) ? (char)data[i] : '.', 0};
                        say(text);
                    }
                    say("\n");
                }
            }
            result(status);
        }
        return;
    }
    if (same(command, "run")) {
        static const char *const programs[] = {"demo", "fault", "io", "spin", "nx", "interrupt", "empty", "abort", "readonly"};
        unsigned i;
        for (i = 0; i < 9; i++) if (name && same(name, programs[i])) break;
        if (i == 9 || *rest) { say("Usage: run demo|fault|io|spin|nx|interrupt|empty|abort|readonly\n"); return; }
        say("Running "); say(programs[i]); say("\n");
        int64_t status = user_demo_run(i);
        say("[exit] "); if (status < 0) say("-");
        number(status < 0 ? UINT64_C(0) - (uint64_t)status : (uint64_t)status); say("\n");
        return;
    }
    if (name) { say("Unexpected arguments\n"); return; }
    if (same(command, "help")) {
        say("MiniOS64 commands:\nhelp clear version mem uptime ls disk save\ncat <name> | write <name> <text> | rm <name>\nprograms | run <program>\n");
        say("Use save to persist files on a dedicated disk. Names are relative; no slash or dot-dot.\n");
    } else if (same(command, "version")) say("MiniOS64 x86_64 experimental kernel; ABI 1.2\n");
    else if (same(command, "clear")) console_clear();
    else if (same(command, "uptime")) { say("Delivered timer ticks="); number(timer_now()); say(" (~100 Hz)\n"); }
    else if (same(command, "mem")) { say("Frames free="); number(pmm_free_count()); say(" managed="); number(pmm_frame_count()); say("\n"); }
    else if (same(command, "disk")) {
        say(store_ready() ? "Disk ready; snapshot=" : "Disk unavailable or corrupt; snapshot=");
        number(store_sequence()); say("\n");
    } else if (same(command, "save")) {
        int64_t status = store_save(filesystem); result(status);
        if (!status) { say("Saved snapshot="); number(store_sequence()); say("\n"); }
    }
    else if (same(command, "programs")) say("demo fault io spin nx interrupt empty abort readonly\n");
    else if (same(command, "ls")) {
        unsigned count = 0;
        for (unsigned i = 0; i < FS_FILES; i++) {
            struct fs_node *node = &filesystem->image.nodes[i];
            if (node->used) { say(node->name); say(" bytes="); number(node->size); say("\n"); count++; }
        }
        if (!count) say("(empty)\n");
    } else say("Unknown command. Type help.\n");
}
_Noreturn void shell_run(void) {
    filesystem = user_filesystem();
    handle_table_init(&files);
    if (fs_root(filesystem, &files, CAP_ALL, &root)) {
        say("ERROR: shell namespace unavailable\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)store_init(filesystem);
    char line[LINE]; unsigned used = 0, overflow = 0;
    say("MiniOS64 shell. Type help. Use save to keep files.\nx64> ");
    for (;;) {
        int c = console_input();
        if (c < 0) { task_yield(); __asm__ volatile ("sti; hlt" ::: "memory"); continue; }
        if (c == '\t') c = ' ';
        if (c == '\n') {
            say("\n");
            if (overflow) say("Line too long; command discarded\n");
            else { line[used] = 0; execute(line); }
            used = overflow = 0; say("x64> ");
        } else if (c == '\b') {
            if (used && !overflow) { used--; say("\b \b"); }
        } else if (c >= 32 && c < 127 && !overflow) {
            if (used == LINE - 1) overflow = 1;
            else { line[used++] = (char)c; char echo[2] = {(char)c, 0}; say(echo); }
        }
    }
}
