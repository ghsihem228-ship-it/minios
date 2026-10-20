#include "process.h"
#include "user_vm.h"
#include "elf.h"
#include "bytes.h"
struct user_context user_context;
uint64_t user_host_sp, user_host_flags;
static uint8_t syscall_stack[16384] __attribute__((section(".bss.syscall_stack"), aligned(4096)));
int64_t user_run(const void *data, size_t size, struct ramfs *fs, cap_rights_t rights) {
    if (user_context.active) return SYS_EBUSY;
    struct elf_plan plan;
    if (!elf_validate(data, size, &plan)) return SYS_EFORMAT;
    /* Generation history persists across launches. */
    static int initialized;
    if (!initialized) { handle_table_init(&user_context.handles); initialized = 1; }
    int64_t result = SYS_ENOSPC;
    for (unsigned i = 0; i < plan.count; i++) {
        struct elf_segment *s = &plan.segments[i];
        unsigned pages = (s->memsz + 4095) / 4096;
        for (unsigned j = 0; j < pages; j++) {
            unsigned slot = (s->address - USER_BASE) / 4096 + j;
            uint64_t flags = s->flags & 1 ? 0 : PTE_NX;
            if (s->flags & 2) flags |= PTE_WRITE;
            if (!vm_user_map(slot, flags)) goto cleanup;
            size_t offset = (size_t)j * 4096, n = s->filesz > offset ? s->filesz - offset : 0;
            if (n > 4096) n = 4096;
            if (n) bytes_copy(vm_user_physical(slot), (const unsigned char *)data + s->offset + offset, n);
        }
    }
    if (!vm_user_map(USER_PAGES - 1, PTE_WRITE | PTE_NX)) goto cleanup;
    handle_t root = 0;
    if (fs && (result = fs_root(fs, &user_context.handles, rights, &root))) goto cleanup;
    user_context.filesystem = fs;
    error_record(&user_context.error, 0, 0, 0);
    user_context.active = 1; user_context.deadline = timer_now();
    user_gate(1, (uintptr_t)(syscall_stack + sizeof(syscall_stack)));
    result = user_enter(plan.entry, USER_END - 16, root);
    user_gate(0, 0);
    user_context.active = 0;
cleanup:
    if (user_context.transaction && fs) (void)fs_tx_end(fs, 1);
    user_context.transaction = 0;
    vm_user_clear();
    handle_table_clear(&user_context.handles);
    return result;
}
void user_finish(struct interrupt_frame *f, int64_t code) {
    f->rip = (uintptr_t)user_return; f->cs = 8; f->ss = 16;
    f->rsp = user_host_sp; f->rflags = user_host_flags;
    f->rax = (uint64_t)code;
}
int user_fault(struct interrupt_frame *f) {
    if (!user_context.active || (f->cs & 3) != 3 || f->vector == 2 || f->vector == 8 || f->vector == 18) return 0;
    if (f->vector == 14 && (f->error & 7) == 7) {
        uint64_t address; __asm__ volatile ("mov %%cr2, %0" : "=r"(address));
        if (vm_user_cow(address)) return 1;
    }
    user_finish(f, -1000 - (int64_t)f->vector); return 1;
}
