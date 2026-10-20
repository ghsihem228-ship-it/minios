#ifndef STARTER_PMM_H
#define STARTER_PMM_H
#include "bootmem.h"
enum frame_state { FRAME_RESERVED, FRAME_FREE, FRAME_OWNED, FRAME_TABLE, FRAME_MAPPED };
/* Single CPU, boot initialization only. Reserved overlaps always win. */
int pmm_init(const struct boot_memory *map, const struct phys_range *reserved, unsigned count);
uint64_t pmm_alloc(void); /* Aligned physical address, or 0; contents unspecified. */
int pmm_free(uint64_t physical); /* Only an owned, unmapped, non-table frame. */
uint64_t pmm_alloc_table(void); /* Page-table ownership; release only after detaching all references. */
int pmm_free_table(uint64_t physical); /* Caller must detach and flush first. */
int pmm_set_mapped(uint64_t physical, int mapped); /* Enforce one arena alias. */
enum frame_state pmm_state(uint64_t physical);
uint64_t pmm_free_count(void);
unsigned pmm_frame_count(void);
#endif
