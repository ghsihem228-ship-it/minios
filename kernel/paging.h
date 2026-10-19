#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE 4096u
#define HEAP_BASE 0xD0000000u
/* Two pages at the end of the heap table are reserved for paging tests. */
#define HEAP_MAX_BYTES (4u * 1024u * 1024u - 2u * PAGE_SIZE)

int paging_init(void);
int paging_enabled(void);
/* Only the heap's 4 MiB window is mutable. All mappings are supervisor RW.
 * Frames belong to the caller; unmapping does not free them. */
int paging_map_page(uint32_t virt, uint32_t phys);
int paging_unmap_page(uint32_t virt);
int paging_translate(uint32_t virt, uint32_t *phys);
int paging_self_test(void);

#endif
