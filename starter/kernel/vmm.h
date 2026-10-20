#ifndef STARTER_VMM_H
#define STARTER_VMM_H
#include "pmm.h"
#define VM_ARENA UINT64_C(0xffff800000000000)
#define VM_ARENA_PAGES 512u
#define PTE_PRESENT UINT64_C(1)
#define PTE_WRITE UINT64_C(2)
#define PTE_NX (UINT64_C(1) << 63)
int memory_init(uint32_t boot_info);
int memory_selftest(void);
/* Single CPU, IF=0, not callable from an ISR. One writable NX arena alias
 * per owned frame; permanent identity aliases are internal to the manager. */
int vm_map_page(uint64_t virtual_address, uint64_t physical);
int vm_unmap_page(uint64_t virtual_address, uint64_t *physical);
int vm_alloc_page(uint64_t virtual_address); /* Zero filled, RW/NX/supervisor. */
int vm_free_page(uint64_t virtual_address);
int vm_lookup(uint64_t virtual_address, uint64_t *physical, uint64_t *flags);
#endif
