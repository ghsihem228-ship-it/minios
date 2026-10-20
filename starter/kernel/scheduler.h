#ifndef STARTER_SCHEDULER_H
#define STARTER_SCHEDULER_H
#include <stdint.h>
#include "handle.h"
/* Single CPU, ring 0, ordinary task context only; never call from an ISR.
 * No stable syscall ABI is introduced. Entries may return (implicit exit). */
void scheduler_init(void);
int task_create(void (*entry)(void *), void *argument);
struct task_grant { handle_t source; cap_rights_t rights; };
/* Trusted kernel launch API. Child tokens are returned separately from source
 * tokens. Arrays are kernel-owned and must not alias live tables/objects. */
int task_create_with_caps(void (*entry)(void *), void *argument,
                          const struct task_grant *, size_t count, handle_t *child_handles);
void task_yield(void);
void task_sleep(uint64_t ticks);
_Noreturn void task_exit(void);
unsigned task_live_count(void); /* excludes bootstrap/idle task */
struct handle_table *task_handle_table(void); /* borrowed, current task only */
int scheduler_selftest(void);
int capability_launch_selftest(void);
#endif
