#ifndef TASK_H
#define TASK_H
#include <stdint.h>
#include "isr.h"
#include "vfs.h"
#include "startup.h"

#define TASK_FDS 8
void task_init(void);
int task_spawn(const char *path);
int task_spawn_args(unsigned argc, const char *const *argv);
int task_kill(uint32_t pid);
int task_wait(uint32_t pid);
void task_list(void);
void task_reap(void);
/* Called only by the kernel task at a safe scheduling point. */
void task_idle(void);
struct regs *task_tick(struct regs *r);
struct regs *task_yield(struct regs *r);
struct regs *task_exit(struct regs *r, int code);
struct regs *task_sleep(struct regs *r, uint32_t ticks);
struct regs *task_fault(struct regs *r);
unsigned task_space(void);
uint32_t task_pid(void);
struct file_handle *task_fd(unsigned fd);
#endif
