#include "process.h"
#include "user_vm.h"
#include "elf.h"
#include "bytes.h"
struct process {
    struct user_execution context;
    struct user_space space;
    struct interrupt_frame frame;
    unsigned char fx[512] __attribute__((aligned(16)));
    uint64_t pid, slices, limit;
    int64_t result;
    enum user_state state;
};
static struct process processes[USER_PROCESSES];
static struct user_execution idle_context;
struct user_execution *current_user = &idle_context;
static struct process *running;
uint64_t user_host_sp, user_host_flags;
static uint64_t next_pid = 1;
static unsigned cursor, initialized;
static uint8_t syscall_stack[16384] __attribute__((section(".bss.syscall_stack"), aligned(4096)));
extern int64_t user_resume(const struct interrupt_frame *);
static uint64_t lock(void) {
    uint64_t flags; __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory"); return flags;
}
static void unlock(uint64_t flags) { if (flags & 512) __asm__ volatile ("sti" ::: "memory"); }
static void initialize(void) {
    if (initialized) return;
    uint64_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0; mov %%cr4, %1" : "=r"(cr0), "=r"(cr4));
    cr0 = (cr0 & ~UINT64_C(12)) | UINT64_C(34); /* Clear EM/TS, set MP/NE. */
    cr4 = (cr4 | UINT64_C(0x600)) & ~UINT64_C(0x50000); /* FXSR/SIMD exceptions; no FSGSBASE/XSAVE. */
    __asm__ volatile ("mov %0, %%cr0; mov %1, %%cr4" :: "r"(cr0), "r"(cr4) : "memory");
    for (unsigned i = 0; i < USER_PROCESSES; i++) handle_table_init(&processes[i].context.handles);
    initialized = 1;
}
static struct process *find(uint64_t pid) {
    for (unsigned i = 0; i < USER_PROCESSES; i++)
        if (processes[i].state != USER_FREE && processes[i].pid == pid) return &processes[i];
    return NULL;
}
/* Kernel-only launch; no user-selected filesystem pointer or ambient grant. */
int64_t user_spawn(const void *data, size_t size, struct ramfs *fs, cap_rights_t rights, uint64_t *pid) {
    *pid = 0;
    struct elf_plan plan;
    if (!elf_validate(data, size, &plan)) return SYS_EFORMAT;
    if (rights & ~CAP_ALL) return SYS_EINVAL;
    uint64_t flags = lock(); initialize();
    if (running || (fs && fs->transaction)) { unlock(flags); return SYS_EBUSY; }
    struct process *p = NULL;
    for (unsigned i = 0; i < USER_PROCESSES; i++) if (processes[i].state == USER_FREE) { p = &processes[i]; break; }
    if (!p || !next_pid) { unlock(flags); return p ? SYS_EOVERFLOW : SYS_ENOSPC; }
    int64_t result = SYS_ENOMEM;
    if (!vm_space_create(&p->space)) { unlock(flags); return result; }
    vm_space_select(&p->space);
    for (unsigned i = 0; i < plan.count; i++) {
        struct elf_segment *s = &plan.segments[i];
        unsigned pages = (s->memsz + 4095) / 4096;
        for (unsigned j = 0; j < pages; j++) {
            unsigned slot = (s->address - USER_BASE) / 4096 + j;
            uint64_t protection = s->flags & 1 ? 0 : PTE_NX;
            if (s->flags & 2) protection |= PTE_WRITE;
            if (!vm_user_map(slot, protection)) goto failed;
            size_t offset = (size_t)j * 4096, n = s->filesz > offset ? s->filesz - offset : 0;
            if (n > 4096) n = 4096;
            if (n) bytes_copy(vm_user_physical(slot), (const unsigned char *)data + s->offset + offset, n);
        }
    }
    if (!vm_user_map(USER_PAGES - 1, PTE_WRITE | PTE_NX)) goto failed;
    handle_t root = 0;
    if (fs && (result = fs_root(fs, &p->context.handles, rights, &root))) goto failed;
    p->context.filesystem = fs; p->context.active = p->context.transaction = 0;
    error_record(&p->context.error, 0, 0, 0);
    bytes_zero(&p->frame, sizeof(p->frame));
    p->frame.rip = plan.entry; p->frame.rsp = USER_END - 16;
    p->frame.cs = 0x2b; p->frame.ss = 0x33; p->frame.rflags = 0x202; p->frame.rdi = root;
    /* Canonical clean FXSAVE image: x87 control word and masked MXCSR defaults.
     * All x87/MMX/XMM payload bytes are zero, never inherited from another task. */
    bytes_zero(p->fx, sizeof(p->fx));
    p->fx[0] = 0x7f; p->fx[1] = 3; p->fx[24] = 0x80; p->fx[25] = 0x1f;
    p->pid = next_pid++; p->slices = 0; p->limit = 0; p->result = 0;
    p->state = USER_READY; *pid = p->pid;
    vm_space_select(NULL); unlock(flags); return SYS_OK;
failed:
    vm_space_destroy(&p->space); handle_table_clear(&p->context.handles);
    vm_space_select(NULL); unlock(flags); return result;
}
/* No suspended kernel syscall stacks: INT80 gates keep IF=0, all services run
 * to completion. A single guarded entry stack is reused only between slices. */
static void host_return(struct interrupt_frame *f, int64_t code) {
    vm_space_kernel();
    f->rip = (uintptr_t)user_return; f->cs = 8; f->ss = 16;
    f->rsp = user_host_sp; f->rflags = user_host_flags;
    f->rax = (uint64_t)code;
}
void user_finish(struct interrupt_frame *f, int64_t code) {
    if (!running) return;
    running->state = USER_EXITED; running->result = code;
    host_return(f, code);
}
void user_tick(struct interrupt_frame *f) {
    if (!running || !user_context.active || (f->cs & 3) != 3) return;
    running->slices++;
    if ((running->limit && running->slices >= running->limit) ||
        (user_context.transaction && timer_now() - user_context.deadline >= 200)) { user_finish(f, -2000); return; }
    running->frame = *f; running->state = USER_READY;
    host_return(f, 0);
}
static void release(struct process *p) {
    if (p->context.transaction && p->context.filesystem) (void)fs_tx_end(p->context.filesystem, 1);
    p->context.transaction = p->context.active = 0;
    vm_space_destroy(&p->space);
    handle_table_clear(&p->context.handles);
}
void user_poll(void) {
    uint64_t flags = lock();
    if (running) { unlock(flags); return; }
    struct process *p = NULL;
    for (unsigned n = 1; n <= USER_PROCESSES; n++) {
        unsigned slot = (cursor + n) % USER_PROCESSES;
        if (processes[slot].state == USER_READY) { p = &processes[slot]; cursor = slot; break; }
    }
    if (!p) { unlock(flags); return; }
    do {
        running = p; current_user = &p->context; p->context.active = 1; p->state = USER_RUNNING;
        vm_space_select(&p->space); vm_space_enter();
        user_gate(1, (uintptr_t)(syscall_stack + sizeof(syscall_stack)));
        __asm__ volatile ("fxrstor64 %0" :: "m"(p->fx) : "memory");
        (void)user_resume(&p->frame);
        __asm__ volatile ("fxsave64 %0" : "=m"(p->fx) :: "memory");
        user_gate(0, 0); p->context.active = 0;
        /* A filesystem transaction owns the shared RAMFS overlay exclusively.
         * Do not run the shell or other processes until commit/abort/watchdog. */
    } while (p->state == USER_READY && p->context.transaction);
    if (p->state == USER_EXITED) release(p);
    vm_space_select(NULL); running = NULL; current_user = &idle_context;
    unlock(flags);
}
int user_info(unsigned slot, struct user_info *out) {
    if (slot >= USER_PROCESSES || processes[slot].state == USER_FREE) return 0;
    struct process *p = &processes[slot];
    *out = (struct user_info){p->pid, p->slices, p->result, p->state}; return 1;
}
int64_t user_reap(uint64_t pid, int64_t *code) {
    uint64_t flags = lock(); struct process *p = find(pid);
    int64_t result = !p ? SYS_ENOENT : p->state != USER_EXITED ? SYS_EAGAIN : SYS_OK;
    if (!result) { *code = p->result; p->state = USER_FREE; }
    unlock(flags); return result;
}
int64_t user_kill(uint64_t pid) {
    uint64_t flags = lock(); struct process *p = find(pid);
    int64_t result = !p ? SYS_ENOENT : p->state != USER_READY ? SYS_EBUSY : SYS_OK;
    if (!result) { release(p); p->result = -2001; p->state = USER_EXITED; }
    unlock(flags); return result;
}
int64_t user_run(const void *data, size_t size, struct ramfs *fs, cap_rights_t rights) {
    uint64_t pid; int64_t code, result = user_spawn(data, size, fs, rights, &pid);
    if (result) return result;
    find(pid)->limit = 200;
    do { user_poll(); result = user_reap(pid, &code); } while (result == SYS_EAGAIN);
    return result ? result : code;
}
int user_fault(struct interrupt_frame *f) {
    if (!user_context.active || (f->cs & 3) != 3 || f->vector == 2 || f->vector == 8 || f->vector == 18) return 0;
    if (f->vector == 14 && (f->error & 7) == 7) {
        uint64_t address; __asm__ volatile ("mov %%cr2, %0" : "=r"(address));
        if (vm_user_cow(address)) return 1;
    }
    user_finish(f, -1000 - (int64_t)f->vector); return 1;
}

/* Real CPU preemption, not cooperative mocks: these images never issue yield.
 * Identical virtual addresses hold different markers and independently advance.
 * Each image continuously checks its own integer, x87 and XMM register state. */
extern const unsigned char user_elf_9[], user_elf_9_end[], user_elf_10[], user_elf_10_end[];
int user_process_selftest(void) {
    uint64_t baseline = pmm_free_count(), a, b, ignored;
    int64_t code;
    /* On small boots exhaust the allocator at each partial-load boundary.
     * No unrelated owner exists here; the private held-frame chain is detached
     * from every candidate space and restored before the next test. */
    if (pmm_frame_count() <= 4096) for (unsigned budget = 0; budget < 7; budget++) {
        uint64_t held = 0, pa;
        while ((pa = pmm_alloc())) { *(uint64_t *)(uintptr_t)pa = held; held = pa; }
        for (unsigned i = 0; i < budget; i++) {
            if (!held) return 0;
            pa = held; held = *(uint64_t *)(uintptr_t)pa; (void)pmm_free(pa);
        }
        if (user_spawn(user_elf_9, (size_t)(user_elf_9_end-user_elf_9), NULL, 0, &ignored) != SYS_ENOMEM ||
            ignored || pmm_free_count() != budget) return 0;
        while (held) { pa = held; held = *(uint64_t *)(uintptr_t)pa; (void)pmm_free(pa); }
        if (pmm_free_count() != baseline) return 0;
    }
    if (user_spawn("bad", 3, NULL, 0, &ignored) != SYS_EFORMAT || ignored || pmm_free_count() != baseline) return 0;
    if (user_spawn(user_elf_9, (size_t)(user_elf_9_end-user_elf_9), NULL, 0, &a) ||
        user_spawn(user_elf_10, (size_t)(user_elf_10_end-user_elf_10), NULL, 0, &b)) return 0;
    struct process *first = find(a), *second = find(b);
    if (!first || !second || first->space.tables[0] == second->space.tables[0]) return 0;
    for (unsigned n = 0; n < 12; n++) user_poll();
    if (first->state != USER_READY || second->state != USER_READY || first->slices < 3 || second->slices < 3) return 0;
    uint64_t values[2], prior[2];
    for (unsigned i = 0; i < 2; i++) {
        struct process *p = i ? second : first;
        vm_space_select(&p->space);
        if (!vm_user_read(values, USER_BASE + PAGE_SIZE, sizeof(values)) || !values[0] || values[1] != 9+i) return 0;
        prior[i] = values[0];
    }
    vm_space_select(NULL);
    for (unsigned n = 0; n < 4; n++) user_poll();
    for (unsigned i = 0; i < 2; i++) {
        struct process *p = i ? second : first;
        vm_space_select(&p->space);
        if (!vm_user_read(values, USER_BASE + PAGE_SIZE, sizeof(values)) || values[0] <= prior[i] || values[1] != 9+i) return 0;
    }
    vm_space_select(NULL);
    if (user_kill(a) || user_reap(a, &code) || code != -2001 || user_kill(a) != SYS_ENOENT) return 0;
    uint64_t before = second->slices;
    user_poll();
    if (second->state != USER_READY || second->slices <= before) return 0;
    if (user_kill(b) || user_reap(b, &code) || code != -2001 || pmm_free_count() != baseline) return 0;
    return 1;
}
