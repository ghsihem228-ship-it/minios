#ifndef STARTER_SCHEDULER_H
#define STARTER_SCHEDULER_H
#include <stdint.h>
#include "handle.h"
/* Single CPU, ring 0, ordinary task context only; never call from an ISR.
 * No stable syscall ABI is introduced. Entries may return (implicit exit). */
void scheduler_init(void);
int task_create(void (*entry)(void *), void *argument);
void task_yield(void);
void task_sleep(uint64_t ticks);
_Noreturn void task_exit(void);
unsigned task_live_count(void); /* excludes bootstrap/idle task */
struct handle_table *task_handle_table(void); /* borrowed, current task only */
int scheduler_selftest(void);
#endif
