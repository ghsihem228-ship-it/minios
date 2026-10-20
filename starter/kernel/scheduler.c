#include "scheduler.h"
#include "interrupts.h"
#include "serial.h"
#include <stddef.h>

#define TASKS 8
#define STACK_SIZE 16384
#define CANARY UINT64_C(0x51acced0feedbabe)
enum state { FREE, READY, RUNNING, SLEEPING, ZOMBIE };
struct task {
    uintptr_t sp;
    enum state state;
    uint64_t since, delay;
    void (*entry)(void *);
    void *argument;
};
static struct task tasks[TASKS];
static unsigned current;
static struct handle_table task_handles[TASKS];
struct handle_table *task_handle_table(void) { return &task_handles[current]; }
/* Slot zero uses the existing bootstrap stack. No allocator dependency. */
static uint64_t stacks[TASKS - 1][STACK_SIZE / 8] __attribute__((aligned(16)));
extern void context_switch(uintptr_t *old_sp, uintptr_t new_sp);
static uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static void irq_restore(uint64_t flags) {
    if (flags & 512) __asm__ volatile ("sti" ::: "memory");
}
static _Noreturn void bug(void) {
    (void)serial_write("ERROR: scheduler invariant\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
/* Runs with IF=0. Never free a stack until executing on a different stack. */
static void schedule(void) {
    uint64_t now = timer_now();
    for (unsigned i = 1; i < TASKS; i++) {
        if (tasks[i].state != FREE && stacks[i - 1][0] != CANARY) bug();
        if (i != current && tasks[i].state == ZOMBIE) tasks[i].state = FREE;
        if (tasks[i].state == SLEEPING && now - tasks[i].since >= tasks[i].delay)
            tasks[i].state = READY;
    }
    unsigned old = current, next = old;
    if (tasks[old].state == RUNNING) tasks[old].state = READY;
    for (unsigned n = 1; n <= TASKS; n++) {
        unsigned candidate = (old + n) % TASKS;
        if (tasks[candidate].state == READY) { next = candidate; break; }
    }
    if (tasks[next].state != READY) bug(); /* bootstrap is always runnable */
    tasks[next].state = RUNNING;
    current = next;
    if (next != old) context_switch(&tasks[old].sp, tasks[next].sp);
}
static _Noreturn void task_start(void) {
    __asm__ volatile ("sti" ::: "memory");
    tasks[current].entry(tasks[current].argument);
    task_exit();
}
void scheduler_init(void) {
    /* Call exactly once, after interrupts_init, with IF=0. */
    current = 0;
    for (unsigned i = 0; i < TASKS; i++) handle_table_init(&task_handles[i]);
    tasks[0].state = RUNNING;
}
int task_create(void (*entry)(void *), void *argument) {
    if (!entry) return -1;
    uint64_t flags = irq_save();
    unsigned slot;
    for (slot = 1; slot < TASKS; slot++) if (tasks[slot].state == FREE) break;
    if (slot == TASKS) { irq_restore(flags); return -1; }
    uint64_t *base = stacks[slot - 1];
    /* Wipe prior task data before reuse. Static stacks are still not isolated. */
    for (unsigned i = 0; i < STACK_SIZE / 8; i++) base[i] = 0;
    base[0] = CANARY;
    uint64_t *sp = base + STACK_SIZE / 8;
    *--sp = 0; /* Dummy return slot: SysV C entry RSP is 8 mod 16. */
    *--sp = (uintptr_t)task_start;
    for (unsigned i = 0; i < 6; i++) *--sp = 0; /* r15..rbp */
    tasks[slot] = (struct task){ .sp = (uintptr_t)sp, .state = READY,
                                .entry = entry, .argument = argument };
    irq_restore(flags);
    return (int)slot; /* private slot, not a public handle or stable task ID */
}
void task_yield(void) {
    uint64_t flags = irq_save();
    schedule();
    irq_restore(flags);
}
void task_sleep(uint64_t ticks) {
    if (!ticks) { task_yield(); return; }
    uint64_t flags = irq_save();
    /* Sleep requires ticks to advance; boot/idle may never block itself. */
    if (!current || !(flags & 512) || ticks > UINT64_MAX / 2) bug();
    tasks[current].since = timer_now();
    tasks[current].delay = ticks;
    tasks[current].state = SLEEPING;
    schedule();
    irq_restore(flags);
}
_Noreturn void task_exit(void) {
    (void)irq_save();
    if (!current) bug();
    handle_table_clear(task_handle_table());
    tasks[current].state = ZOMBIE;
    schedule();
    bug(); /* ZOMBIE contexts must never be resumed. */
}
unsigned task_live_count(void) {
    uint64_t flags = irq_save();
    unsigned count = 0;
    for (unsigned i = 1; i < TASKS; i++)
        if (tasks[i].state != FREE && tasks[i].state != ZOMBIE) count++;
    irq_restore(flags);
    return count;
}

/* Integration checks use real stacks, timer interrupts and returning entries. */
static unsigned progress[2], failed, handle_destroyed;
static struct object worker_objects[2];
static void worker_destroy(struct object *o) { (void)o; handle_destroyed++; }
static void worker(void *argument) {
    unsigned index = (unsigned)(uintptr_t)argument;
    handle_t handle;
    struct object *object = &worker_objects[index];
    if (object_init(object, OBJECT_TEST, worker_destroy) != OBJ_OK ||
        handle_install(task_handle_table(), object, &handle) != OBJ_OK) {
        failed = 1;
        return;
    }
    object_put(object); /* task's handle now owns the only reference */
    volatile uint64_t local = UINT64_C(0x123456789abcdef0) + index;
    for (unsigned i = 0; i < 12; i++) {
        if (local != UINT64_C(0x123456789abcdef0) + index) failed = 1;
        struct object *pin;
        if (handle_lookup(task_handle_table(), handle, OBJECT_TEST, &pin) != OBJ_OK)
            failed = 1;
        else {
            if (pin != object) failed = 1;
            object_put(pin);
        }
        progress[index]++;
        task_sleep(0); /* zero-duration sleep is a yield */
        uint64_t start = timer_now();
        task_sleep(2);
        if (timer_now() - start < 2) failed = 1;
    }
    if (index) task_exit(); /* exercise explicit exit as well as return */
}
static void returning_worker(void *unused) { (void)unused; }
int scheduler_selftest(void) {
    if (task_create(0, 0) != -1) return 0;
    if (task_create(worker, (void *)0) < 0 || task_create(worker, (void *)1) < 0)
        return 0;
    uint64_t start = timer_now();
    while (task_live_count()) {
        task_yield();
        if (timer_now() - start > 1000) return 0;
        __asm__ volatile ("sti; hlt" ::: "memory");
    }
    task_yield(); /* reap final zombie on a different stack */
    if (failed || progress[0] != 12 || progress[1] != 12 || handle_destroyed != 2) return 0;
    for (unsigned round = 0; round < 16; round++) {
        for (unsigned i = 1; i < TASKS; i++)
            if (task_create(returning_worker, 0) < 0) return 0;
        if (task_create(returning_worker, 0) != -1) return 0;
        task_yield();
        task_yield();
        if (task_live_count()) return 0;
    }
    return 1;
}
