#ifndef STARTER_STORE_H
#define STARTER_STORE_H
#include "fs.h"
#define STORE_ENTRY (8u + 32u + FS_BYTES)
#define STORE_BYTES (FS_FILES * STORE_ENTRY)
#define STORE_SECTORS ((STORE_BYTES + 511u) / 512u)
uint32_t store_hash(const void *, size_t);
int store_decode(const void *, size_t, struct fs_image *);
void store_encode(const struct fs_image *, void *);
int64_t store_init(struct ramfs *);
int64_t store_save(struct ramfs *);
int store_ready(void);
uint64_t store_sequence(void);
#endif
