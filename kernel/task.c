/* Preemptive user processes, non-preemptible kernel paths, single CPU.
 * Switching happens by returning a different interrupt frame to IRET. */
#include "task.h"
#include "paging.h"
#include "elf.h"
#include "gdt.h"
#include "timer.h"
#include "vga.h"
#include "keyboard.h"
#include "heap.h"
#include "net.h"
#include "socket.h"
#include <stddef.h>

#define TASKS (MAX_USER_SPACES + 1)
#define STACK_SIZE 8192
#define QUANTUM 5u
enum state { UNUSED, READY, RUNNING, SLEEPING, ZOMBIE };
struct task {
    struct regs *context;
    uint32_t pid, wake, ticks, switches;
    int code, reaped;
    enum state state;
    char name[FS_PATH_MAX];
    struct file_handle files[TASK_FDS];
};
static struct task tasks[TASKS];
static uint8_t stacks[MAX_USER_SPACES][STACK_SIZE] __attribute__((aligned(16)));
static unsigned current;
static uint32_t next_pid = 1, budget = QUANTUM;
static int ready;
static volatile int kernel_idle;

void task_init(void) {
    tasks[0].state = RUNNING;
    tasks[0].name[0] = 'k'; tasks[0].name[1] = 'e'; tasks[0].name[2] = 'r';
    tasks[0].name[3] = 'n'; tasks[0].name[4] = 'e'; tasks[0].name[5] = 'l';
    ready = 1;
}

static struct regs *schedule(struct regs *r) {
    tasks[current].context = r;
    if (tasks[current].state == RUNNING) tasks[current].state = READY;
    unsigned next = current;
    for (unsigned n = 1; n <= TASKS; n++) {
        unsigned candidate = (current + n) % TASKS;
        if (tasks[candidate].state == READY) { next = candidate; break; }
    }
    current = next;
    tasks[current].state = RUNNING;
    tasks[current].switches++;
    budget = QUANTUM;
    paging_space_activate(current ? (int)current - 1 : -1);
    if (current) tss_set_stack((uint32_t)&stacks[current - 1][STACK_SIZE]);
    return tasks[current].context;
}

struct regs *task_tick(struct regs *r) {
    if (!ready) return r;
    uint32_t now = timer_ticks();
    for (unsigned i = 1; i < TASKS; i++)
        if (tasks[i].state == SLEEPING && (int32_t)(now - tasks[i].wake) >= 0)
            tasks[i].state = READY;
    tasks[current].ticks++;
    /* Never suspend an arbitrary kernel path. Task 0 explicitly permits
     * scheduling around HLT. User syscall gates keep IF clear. */
    if (!current && !kernel_idle) return r;
    if (--budget) return r;
    return schedule(r);
}

void task_idle(void) {
    __asm__ volatile ("cli" ::: "memory");
    kernel_idle = 1;
    __asm__ volatile ("sti; hlt; cli" ::: "memory");
    kernel_idle = 0;
    __asm__ volatile ("sti" ::: "memory");
}

void task_reap(void) {
    if (current) return;
    for (unsigned i = 1; i < TASKS; i++) {
        struct task *t = &tasks[i];
        if (t->state != ZOMBIE || t->reaped) continue;
        socket_reap(t->pid);
        for (unsigned j = 0; j < TASK_FDS; j++) fs_close(&t->files[j]);
        paging_space_destroy(i - 1);
        t->reaped = 1;
    }
}

int task_spawn(const char *path) {
    return task_spawn_args(1, &path);
}
int task_spawn_args(unsigned argc, const char *const *argv) {
    if (!ready || current || next_pid > 0x7FFFFFFFu) return -11;
    struct startup_stack initial;
    int packed = startup_build(argc, argv, USER_TOP, USER_STACK_BOTTOM, &initial);
    if (packed) return packed;
    const char *path = argv[0];
    task_reap();
    uint32_t len;
    uint8_t *image;
    int read_error = fs_read_all(path, &image, &len);
    if (read_error) return read_error;
    unsigned i;
    for (i = 1; i < TASKS; i++)
        if (tasks[i].state == UNUSED || (tasks[i].state == ZOMBIE && tasks[i].reaped)) break;
    if (i == TASKS) { kfree(image); heap_trim(); return -11; }
    uint32_t entry;
    int error = elf_load(i - 1, image, len, &entry);
    kfree(image); heap_trim();
    if (error) return error;
    if (!paging_space_write(i - 1, initial.sp, initial.bytes, initial.used)) {
        paging_space_destroy(i - 1); return -12;
    }
    struct task *t = &tasks[i];
    for (unsigned j = 0; j < sizeof(*t); j++) ((uint8_t *)t)[j] = 0;
    unsigned j = 0;
    while (path[j] && j < sizeof(t->name) - 1) { t->name[j] = path[j]; j++; }
    t->pid = next_pid++;
    struct user_frame { struct regs r; uint32_t esp, ss; };
    _Static_assert(sizeof(struct user_frame) == 64, "IRET user frame");
    struct user_frame *frame = (void *)&stacks[i - 1][STACK_SIZE - sizeof(*frame)];
    for (j = 0; j < sizeof(*frame); j++) ((uint8_t *)frame)[j] = 0;
    frame->r.ds = GDT_USER_DATA; frame->r.cs = GDT_USER_CODE;
    frame->r.eip = entry; frame->r.eflags = 0x202;
    frame->esp = initial.sp; frame->ss = GDT_USER_DATA;
    t->context = &frame->r;
    t->state = READY;
    return t->pid;
}

struct regs *task_yield(struct regs *r) { return schedule(r); }
struct regs *task_exit(struct regs *r, int code) {
    tasks[current].code = code;
    tasks[current].state = ZOMBIE;
    return schedule(r);
}
struct regs *task_sleep(struct regs *r, uint32_t ticks) {
    if (ticks) {
        tasks[current].wake = timer_ticks() + ticks;
        tasks[current].state = SLEEPING;
    }
    return schedule(r);
}
unsigned task_space(void) { return current - 1; }
uint32_t task_pid(void) { return tasks[current].pid; }
struct file_handle *task_fd(unsigned fd) {
    return current && fd < TASK_FDS ? &tasks[current].files[fd] : NULL;
}
struct regs *task_fault(struct regs *r) {
    vga_puts("\n[process fault] pid="); vga_put_dec(task_pid());
    vga_puts(" vector="); vga_put_dec(r->int_no);
    vga_puts(" err="); vga_put_hex(r->err_code);
    vga_puts(" eip="); vga_put_hex(r->eip);
    if (r->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        vga_puts("\nuser cr2="); vga_put_hex(cr2);
    }
    vga_putc('\n');
    return task_exit(r, 128 + r->int_no);
}
int task_kill(uint32_t pid) {
    if (!pid) return -1;
    for (unsigned i = 1; i < TASKS; i++)
        if (tasks[i].pid == pid && tasks[i].state != UNUSED && tasks[i].state != ZOMBIE) {
            tasks[i].code = 137; tasks[i].state = ZOMBIE; task_reap(); return 0;
        }
    return -3;
}
int task_wait(uint32_t pid) {
    for (;;) {
        net_poll();
        for (unsigned i = 1; i < TASKS; i++) if (tasks[i].pid == pid) {
            if (tasks[i].state == ZOMBIE) {
                int code = tasks[i].code; task_reap(); return code;
            }
            if (keyboard_poll() == 27) task_kill(pid);
            task_idle();
            goto again;
        }
        return -3;
again:;
    }
}
void task_list(void) {
    static const char *names[] = {"unused", "ready", "running", "sleep", "zombie"};
    task_reap();
    vga_puts("PID STATE TICKS SWITCHES EXIT NAME\n");
    for (unsigned i = 0; i < TASKS; i++) if (tasks[i].state != UNUSED) {
        struct task *t = &tasks[i];
        vga_put_dec(t->pid); vga_putc(' '); vga_puts(names[t->state]); vga_putc(' ');
        vga_put_dec(t->ticks); vga_putc(' '); vga_put_dec(t->switches); vga_putc(' ');
        if (t->state == ZOMBIE) vga_put_dec((uint32_t)t->code);
        else vga_putc('-');
        vga_putc(' '); vga_puts(t->name); vga_putc('\n');
    }
}
