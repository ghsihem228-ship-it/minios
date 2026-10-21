#ifndef STARTER_PROCESS_H
#define STARTER_PROCESS_H
#include "error.h"
#include "fs.h"
#include "interrupts.h"
struct user_execution {
    int active, transaction;
    struct handle_table handles;
    struct error_chain error;
    struct ramfs *filesystem;
    uint64_t deadline;
    struct interrupt_frame transaction_frame;
    unsigned char transaction_fx[512] __attribute__((aligned(16)));
};
extern struct user_execution *current_user;
#define user_context (*current_user)
#define USER_PROCESSES 4u
enum user_state { USER_FREE, USER_READY, USER_RUNNING, USER_EXITED, USER_WAITING };
struct user_info { uint64_t pid, slices; int64_t result; enum user_state state; unsigned managed; };
int64_t user_spawn(const void *elf, size_t, struct ramfs *, cap_rights_t, uint64_t *pid);
void user_poll(void); /* At most one timer slice, except an exclusive transaction. */
int64_t user_kill(uint64_t pid);
int64_t user_reap(uint64_t pid, int64_t *code);
int user_info(unsigned slot, struct user_info *);
void user_tick(struct interrupt_frame *);
int user_process_selftest(void);
int64_t user_run(const void *elf, size_t, struct ramfs *, cap_rights_t initial_rights);
int user_fault(struct interrupt_frame *);
void user_finish(struct interrupt_frame *, int64_t);
int process_services(struct interrupt_frame *, struct syscall_result *);
int services_dispatch(struct interrupt_frame *, struct syscall_result *);
int user_selftest(void);
struct ramfs *user_filesystem(void);
int64_t user_demo_run(unsigned index);
int64_t user_demo_spawn(unsigned index, uint64_t *pid);

extern uint64_t user_host_sp, user_host_flags;
extern void user_return(void);
void user_gate(int enable, uint64_t kernel_stack);
#endif
