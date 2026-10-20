#ifndef STARTER_SYSCALL_H
#define STARTER_SYSCALL_H
#include "syscall_abi.h"
#include "handle.h"
struct interrupt_frame;
struct syscall_result syscall_execute(struct handle_table *, uint64_t, const uint64_t[6]);
void syscall_dispatch(struct interrupt_frame *);
int syscall_selftest(void);
int syscall_register_probe(void);
#endif
