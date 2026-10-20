#include "syscall.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "task.h"
#include "timer.h"
#include "vga.h"
#include <stddef.h>

extern void syscall_entry(void);
#define IO_LIMIT 1024u

void syscall_install(void) {
    /* Interrupt gate, DPL 3: transitions to TSS.esp0 with IF cleared. */
    idt_set_gate(128, (uint32_t)syscall_entry, GDT_KERNEL_CODE, 0xEE);
}
static int user_buffer(uint32_t ptr, uint32_t len, int write) {
    return !len || paging_user_range(task_space(), ptr, len, write);
}
static int path_copy(uint32_t ptr, char *path) {
    for (unsigned i = 0; i < FS_PATH_MAX; i++) {
        if (ptr > UINT32_MAX - i || !user_buffer(ptr + i, 1, 0)) return -14;
        path[i] = *(const char *)(ptr + i);
        if (!path[i]) return 0;
    }
    return -36;
}
struct regs *syscall_dispatch(struct regs *r) {
    if ((r->cs & 3) != 3) { r->eax = (uint32_t)-1; return r; }
    int result = -38;
    switch (r->eax) {
    case 1: /* console write(ptr, len) */
        if (r->ecx > IO_LIMIT) { result = -7; break; }
        if (!user_buffer(r->ebx, r->ecx, 0)) { result = -14; break; }
        for (uint32_t i = 0; i < r->ecx; i++) vga_putc(((const char *)r->ebx)[i]);
        result = r->ecx;
        break;
    case 2: return task_exit(r, (int)(r->ebx & 255));
    case 3: r->eax = 0; return task_yield(r);
    case 4: result = task_pid(); break;
    case 5: result = timer_ticks(); break;
    case 6:
        if (r->ebx > 0x7FFFFFFFu) { result = -22; break; }
        r->eax = 0;
        return task_sleep(r, r->ebx);
    case 7: { /* open(path, flags), per-process descriptor table */
        char path[FS_PATH_MAX];
        result = path_copy(r->ebx, path);
        if (result) break;
        result = -24;
        for (unsigned fd = 0; fd < TASK_FDS; fd++) {
            struct file_handle *f = task_fd(fd);
            if (f->node) continue;
            result = fs_open(f, path, r->ecx);
            if (!result) result = fd;
            break;
        }
        break;
    }
    case 8: /* read(fd, buffer, count) */
    case 10: { /* write(fd, buffer, count) */
        struct file_handle *f = task_fd(r->ebx);
        if (!f || !f->node) { result = -9; break; }
        if (r->edx > IO_LIMIT) { result = -7; break; }
        int read = r->eax == 8;
        if (!user_buffer(r->ecx, r->edx, read)) { result = -14; break; }
        result = read ? fs_read(f, (void *)r->ecx, r->edx) :
                        fs_write(f, (const void *)r->ecx, r->edx);
        break;
    }
    case 9: {
        struct file_handle *f = task_fd(r->ebx);
        if (!f || !f->node) { result = -9; break; }
        result = fs_close(f); break;
    }
    case 11: {
        struct file_handle *f = task_fd(r->ebx);
        result = f ? fs_seek(f, r->ecx) : -9; break;
    }
    case 12: {
        char path[FS_PATH_MAX];
        result = path_copy(r->ebx, path);
        if (!result) result = fs_unlink(path);
        break;
    }
    case 13: result = fs_sync(); break;
    case 14: {
        char path[FS_PATH_MAX]; struct fs_stat stat;
        result = path_copy(r->ebx, path);
        if (result) break;
        if (!user_buffer(r->ecx, sizeof(stat), 1)) { result = -14; break; }
        result = fs_stat(path, &stat);
        if (!result) *(struct fs_stat *)r->ecx = stat;
        break;
    }
    default: break;
    }
    r->eax = result;
    return r;
}
