#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PMM_FRAME_SIZE 4096

/* Reads the Multiboot memory map and builds the frame bitmap. */
void pmm_init(uint32_t mb_info);

/* Returns the physical address of a free 4 KiB frame, or 0 if memory is
 * exhausted (frame 0 is never handed out, so 0 is a safe error value). */
uint32_t pmm_alloc_frame(void);

/* Gives a frame back. Freeing a reserved or already-free frame is ignored. */
void pmm_free_frame(uint32_t phys_addr);

uint32_t pmm_total_frames(void);   /* frames that exist and are usable RAM */
uint32_t pmm_free_frames(void);
uint32_t pmm_used_frames(void);

/* Prints the memory map reported by the bootloader. */
void pmm_print_map(uint32_t mb_info);

#endif
