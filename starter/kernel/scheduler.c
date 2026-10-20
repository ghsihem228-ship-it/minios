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
static struct error_chain task_errors[TASKS];
struct error_chain *task_error_chain(void) { return &task_errors[current]; }
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
int task_create_with_caps(void (*entry)(void *), void *argument,
                          const struct task_grant *grants, size_t count, handle_t *child_handles) {
    if (count > HANDLE_SLOTS || (count && (!grants || !child_handles))) return -1;
    for (size_t i = 0; i < count; i++) child_handles[i] = HANDLE_INVALID;
    if (!entry) return -1;
    uint64_t flags = irq_save();
    unsigned slot;
    for (slot = 1; slot < TASKS; slot++) if (tasks[slot].state == FREE) break;
    if (slot == TASKS) { irq_restore(flags); return -1; }
    for (size_t i = 0; i < count; i++) {
        if (handle_grant(task_handle_table(), grants[i].source, &task_handles[slot],
                         grants[i].rights, &child_handles[i]) != OBJ_OK) {
            handle_table_clear(&task_handles[slot]);
            for (size_t j = 0; j < count; j++) child_handles[j] = HANDLE_INVALID;
            irq_restore(flags);
            return -1; /* Not published: no partially authorized child can run. */
        }
    }
    uint64_t *base = stacks[slot - 1];
    /* Wipe prior task data before reuse. Static stacks are still not isolated. */
    for (unsigned i = 0; i < STACK_SIZE / 8; i++) base[i] = 0;
    base[0] = CANARY;
    uint64_t *sp = base + STACK_SIZE / 8;
    *--sp = 0; /* Dummy return slot: SysV C entry RSP is 8 mod 16. */
    *--sp = (uintptr_t)task_start;
    for (unsigned i = 0; i < 6; i++) *--sp = 0; /* r15..rbp */
    error_record(&task_errors[slot], SYS_OK, 0, 0);
    tasks[slot] = (struct task){ .sp = (uintptr_t)sp, .state = READY,
                                .entry = entry, .argument = argument };
    irq_restore(flags);
    return (int)slot; /* private slot, not a public handle or stable task ID */
}
int task_create(void (*entry)(void *), void *argument) {
    return task_create_with_caps(entry, argument, NULL, 0, NULL);
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
        handle_install(task_handle_table(), object, CAP_ALL, &handle) != OBJ_OK) {
        failed = 1;
        return;
    }
    object_put(object); /* task's handle now owns the only reference */
    volatile uint64_t local = UINT64_C(0x123456789abcdef0) + index;
    for (unsigned i = 0; i < 12; i++) {
        if (local != UINT64_C(0x123456789abcdef0) + index) failed = 1;
        struct object *pin;
        if (handle_lookup(task_handle_table(), handle, OBJECT_TEST, CAP_READ, &pin) != OBJ_OK)
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
static void returning_worker(void *unused) {
    (void)unused;
    /* Ordinary creation inherits nothing, including when a task slot is reused. */
    for (unsigned i = 0; i < HANDLE_SLOTS; i++)
        if (task_handle_table()->slots[i].object) failed = 1;
}
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
        if (task_live_count() || failed) return 0;
    }
    return 1;
}

static handle_t child_handle;
static unsigned launch_seen, launch_failed, launch_destroyed;
static void launch_destroy(struct object *o) { (void)o; launch_destroyed++; }
static void capability_child(void *argument) {
    (void)argument;
    struct object *pin;
    if (handle_lookup(task_handle_table(), child_handle, OBJECT_TEST, CAP_READ, &pin) != OBJ_OK)
        launch_failed = 1;
    else object_put(pin);
    if (handle_lookup(task_handle_table(), child_handle, OBJECT_TEST, CAP_WRITE, &pin) != OBJ_DENIED)
        launch_failed = 1;
    handle_t copy;
    if (handle_duplicate(task_handle_table(), child_handle, &copy) != OBJ_DENIED)
        launch_failed = 1;
    launch_seen++;
    /* Leave delegated handle open: task_exit must release it. */
}
int capability_launch_selftest(void) {
    struct object object;
    handle_t parent;
    if (object_init(&object, OBJECT_TEST, launch_destroy) != OBJ_OK ||
        handle_install(task_handle_table(), &object, CAP_ALL, &parent) != OBJ_OK) return 0;
    struct task_grant invalid[2] = { {parent, CAP_READ}, {parent, CAP_ALL | (UINT64_C(1) << 63)} };
    handle_t partial[2] = {1, 1};
    if (task_create_with_caps(capability_child, NULL, invalid, 2, partial) != -1 ||
        partial[0] || partial[1] || object.refs != 2 || task_live_count() || launch_seen) return 0;
    struct task_grant grant = { parent, CAP_READ };
    if (task_create_with_caps(capability_child, NULL, &grant, 1, &child_handle) < 0) return 0;
    /* Parent close does not revoke an independent child's reference. */
    if (handle_close(task_handle_table(), parent) != OBJ_OK) return 0;
    task_yield();
    task_yield();
    if (task_live_count() || launch_seen != 1 || launch_failed || object.refs != 1) return 0;
    object_put(&object);
    return launch_destroyed == 1;
}
