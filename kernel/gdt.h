#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* Segment selectors (index * 8) */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x1B
#define GDT_USER_DATA 0x23

void gdt_init(void);
void tss_set_stack(uint32_t top);

#endif
