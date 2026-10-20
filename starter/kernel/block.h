#ifndef STARTER_BLOCK_H
#define STARTER_BLOCK_H
#include <stdint.h>
/* Primary-master ATA PIO, LBA28, 512-byte sectors; polled, single CPU. */
int block_init(void);
uint32_t block_sectors(void);
int block_read(uint32_t lba, void *sector);
int block_write(uint32_t lba, const void *sector);
int block_flush(void);
#endif
