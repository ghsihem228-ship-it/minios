#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

struct heap_stats {
    uint32_t committed, used, free, largest_free, blocks;
};

/* Single CPU, task context only: IRQ handlers must not allocate or free.
 * Payloads are 16-byte aligned. Zero size / exhaustion return NULL.
 * Invalid and duplicate frees are ignored. Pages are retained for reuse. */
int heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
struct heap_stats heap_get_stats(void);
int heap_validate(void);
uint32_t heap_trim(void); /* return fully free trailing pages to the PMM */
/* Run only with no outstanding allocations; trims stress-test pages on exit. */
int heap_self_test(void);

#endif
