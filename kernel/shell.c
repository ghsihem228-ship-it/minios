/* Small bounded shell. Memory commands use slot IDs, never raw pointers. */
#include "shell.h"
#include "heap.h"
#include "keyboard.h"
#include "paging.h"
#include "pmm.h"
#include "timer.h"
#include "vga.h"
#include "task.h"
#include "ramfs.h"

#define LINE_SIZE 128u
#define SLOT_COUNT 16u
static void *slots[SLOT_COUNT];

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int number(const char *s, uint32_t *result) {
    uint32_t base = 10, value = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16; s += 2;
    }
    if (!*s) return 0;
    for (; *s; s++) {
        uint32_t digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else return 0;
        if (digit >= base || value > (UINT32_MAX - digit) / base)
            return 0;
        value = value * base + digit;
    }
    *result = value;
    return 1;
}

static void field(const char *label, uint32_t value) {
    vga_puts(label); vga_put_dec(value);
}

static void show_heap(void) {
    struct heap_stats s = heap_get_stats();
    field("Heap: committed=", s.committed);
    field(" used=", s.used); field(" free=", s.free);
    field("\nLargest free=", s.largest_free); field(" blocks=", s.blocks);
    vga_puts(heap_validate() ? " integrity=OK\n" : " integrity=FAIL\n");
}

static void error(int code) {
    vga_puts("Error errno="); vga_put_dec((uint32_t)-code); vga_putc('\n');
}

static void execute(char *line, uint32_t mb_info) {
    char *args[3];
    uint32_t count = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (count == 3) { vga_puts("Too many arguments.\n"); return; }
        args[count++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    if (!count) return;
    int takes_number = equal(args[0], "alloc") || equal(args[0], "free") ||
        equal(args[0], "page") || equal(args[0], "kill");
    int takes_path = equal(args[0], "run") || equal(args[0], "spawn") ||
        equal(args[0], "cat") || equal(args[0], "rm");
    uint32_t expected = equal(args[0], "write") ? 3u :
        ((takes_number || takes_path) ? 2u : 1u);
    if (count != expected) {
        vga_puts("Invalid arguments. Type help.\n"); return;
    }
    uint32_t value = 0;
    if (takes_number && !number(args[1], &value)) {
        vga_puts("Invalid number (decimal or 0x hex required).\n"); return;
    }
    if (equal(args[0], "help")) {
        vga_puts("help clear uptime mem mmap heap trim test\n"
                 "alloc <bytes>  - allocate in one of 16 slots\n"
                 "free <id>      - release an occupied slot\n"
                 "page <addr>    - show virtual-to-physical mapping\n"
                 "ls cat <path> write <path> <word> rm <path>\n"
                 "run <path>     - run an ELF program; Esc cancels\n"
                 "spawn <path>   - start a background ELF process\n"
                 "ps kill <pid>  - process states / termination\n"
                 "fault          - deliberately read NULL; HALTS kernel\n");
    } else if (equal(args[0], "clear")) {
        vga_clear();
    } else if (equal(args[0], "uptime")) {
        field("Uptime ticks (100 Hz): ", timer_ticks()); vga_putc('\n');
    } else if (equal(args[0], "mem")) {
        field("PMM: total=", pmm_total_frames());
        field(" free=", pmm_free_frames()); field(" used=", pmm_used_frames());
        vga_puts(" frames (4 KiB each)\nPaging: ");
        vga_puts(paging_enabled() ? "ON; NULL unmapped\n" : "OFF\n");
        show_heap();
    } else if (equal(args[0], "mmap")) {
        pmm_print_map(mb_info);
    } else if (equal(args[0], "heap")) {
        show_heap();
    } else if (equal(args[0], "trim")) {
        field("Released heap bytes=", heap_trim()); vga_putc('\n');
    } else if (equal(args[0], "ls")) {
        fs_list();
    } else if (equal(args[0], "cat")) {
        uint32_t len;
        const uint8_t *data = fs_image(args[1], &len);
        if (!data) { error(-2); return; }
        if (len >= 4 && data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
            vga_puts("ELF binary; use run or spawn.\n"); return;
        }
        for (uint32_t i = 0; i < len; i++)
            vga_putc(data[i] == '\n' || (data[i] >= 32 && data[i] <= 126) ? data[i] : '.');
        if (!len || data[len - 1] != '\n') vga_putc('\n');
    } else if (equal(args[0], "write")) {
        struct file_handle f = {0};
        int status = fs_open(&f, args[1], FS_WRITE | FS_CREATE | FS_TRUNC);
        if (status) { error(status); return; }
        uint32_t len = 0;
        while (args[2][len]) len++;
        status = fs_write(&f, args[2], len); fs_close(&f);
        if (status < 0) error(status);
        else { field("Wrote bytes=", status); vga_putc('\n'); }
    } else if (equal(args[0], "rm")) {
        int status = fs_unlink(args[1]);
        if (status) error(status); else vga_puts("Removed file.\n");
    } else if (equal(args[0], "run") || equal(args[0], "spawn")) {
        int pid = task_spawn(args[1]);
        if (pid < 0) { error(pid); return; }
        field("Started PID ", pid); vga_putc('\n');
        if (equal(args[0], "run")) {
            int code = task_wait(pid);
            field("[exit] pid=", pid); field(" status=", code); vga_putc('\n');
        }
    } else if (equal(args[0], "ps")) {
        task_list();
    } else if (equal(args[0], "kill")) {
        int status = task_kill(value);
        if (status) error(status); else { field("Killed PID ", value); vga_putc('\n'); }
    } else if (equal(args[0], "alloc")) {
        uint32_t id;
        for (id = 0; id < SLOT_COUNT && slots[id]; id++);
        if (id == SLOT_COUNT) { vga_puts("No free slots.\n"); return; }
        slots[id] = kmalloc(value);
        if (!slots[id]) { vga_puts("Allocation failed.\n"); return; }
        field("Allocated slot ", id); vga_puts(" at ");
        vga_put_hex((uint32_t)slots[id]); field(" bytes=", value); vga_putc('\n');
    } else if (equal(args[0], "free")) {
        if (value >= SLOT_COUNT || !slots[value]) {
            vga_puts("Invalid or empty slot.\n"); return;
        }
        kfree(slots[value]); slots[value] = 0;
        field("Freed slot ", value); vga_putc('\n');
    } else if (equal(args[0], "page")) {
        uint32_t phys;
        vga_put_hex(value);
        if (paging_translate(value, &phys)) {
            vga_puts(" -> "); vga_put_hex(phys); vga_putc('\n');
        } else vga_puts(" -> unmapped\n");
    } else if (equal(args[0], "test")) {
        if (heap_get_stats().used) {
            vga_puts("Free all slots before running tests.\n"); return;
        }
        vga_puts(paging_self_test() ? "[PASS] Paging self-test\n" :
                                    "[FAIL] Paging self-test\n");
        vga_puts(heap_self_test() ? "[PASS] Heap self-test\n" :
                                  "[FAIL] Heap self-test\n");
    } else if (equal(args[0], "fault")) {
        vga_puts("Deliberate NULL read:\n");
        /* Inline asm keeps the intentional fault out of C's NULL UB. */
        __asm__ volatile ("movl 0, %%eax" ::: "eax", "memory");
    } else {
        vga_puts("Unknown command. Type help.\n");
    }
}

void shell_run(uint32_t mb_info) {
    char line[LINE_SIZE];
    uint32_t len = 0;
    int overflow = 0;
    vga_puts("\nMiniOS shell. Type help for commands.\nminios> ");
    for (;;) {
        char c = keyboard_getchar();
        if (c == '\n') {
            vga_putc('\n'); line[len] = 0;
            if (overflow) vga_puts("Line too long; command discarded.\n");
            else execute(line, mb_info);
            len = 0; overflow = 0;
            vga_puts("minios> ");
        } else if (c == '\b') {
            if (len && !overflow) { len--; vga_putc('\b'); }
        } else if ((c >= ' ' && c <= '~') || c == '\t') {
            if (len + 1 < LINE_SIZE && !overflow) {
                if (c == '\t') c = ' ';
                line[len++] = c; vga_putc(c);
            } else overflow = 1;
        }
    }
}
