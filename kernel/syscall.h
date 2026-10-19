#ifndef SYSCALL_H
#define SYSCALL_H
#include "isr.h"
void syscall_install(void);
struct regs *syscall_dispatch(struct regs *r);
#endif
