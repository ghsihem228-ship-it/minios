#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE 4096u
#define HEAP_BASE 0xD0000000u
/* Two pages at the end of the heap table are reserved for paging tests. */
#define HEAP_MAX_BYTES (4u * 1024u * 1024u - 2u * PAGE_SIZE)
#define USER_BASE 0x40000000u
#define USER_TOP  0x40400000u
#define USER_STACK_BOTTOM (USER_TOP - 4u * PAGE_SIZE)
#define USER_IMAGE_END (USER_BASE + 3u * 1024u * 1024u)
#define MAX_USER_SPACES 7

int paging_init(void);
int paging_enabled(void);
/* Only the heap's 4 MiB window is mutable. All mappings are supervisor RW.
 * Frames belong to the caller; unmapping does not free them. */
int paging_map_page(uint32_t virt, uint32_t phys);
int paging_unmap_page(uint32_t virt);
int paging_translate(uint32_t virt, uint32_t *phys);
int paging_self_test(void);
/* Process tables are static; user frames are private, zeroed and reclaimable. */
void paging_space_init(unsigned slot);
void paging_space_destroy(unsigned slot);
void paging_space_activate(int slot); /* -1 selects the kernel directory */
int paging_user_map(unsigned slot, uint32_t virt, int writable);
int paging_space_write(unsigned slot, uint32_t virt, const void *src, uint32_t len);
int paging_user_range(unsigned slot, uint32_t addr, uint32_t len, int write);

#endif
