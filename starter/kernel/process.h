#ifndef STARTER_PROCESS_H
#define STARTER_PROCESS_H
#include "error.h"
#include "fs.h"
#include "interrupts.h"
struct user_context {
    int active, transaction;
    struct handle_table handles;
    struct error_chain error;
    struct ramfs *filesystem;
    uint64_t deadline;
};
extern struct user_context user_context;
int64_t user_run(const void *elf, size_t, struct ramfs *, cap_rights_t initial_rights);
int user_fault(struct interrupt_frame *);
void user_finish(struct interrupt_frame *, int64_t);
int services_dispatch(struct interrupt_frame *, struct syscall_result *);
int user_selftest(void);
struct ramfs *user_filesystem(void);
int64_t user_demo_run(unsigned index);
int64_t user_enter(uint64_t entry, uint64_t stack, uint64_t root_handle);
extern uint64_t user_host_sp, user_host_flags;
extern void user_return(void);
void user_gate(int enable, uint64_t kernel_stack);
#endif
