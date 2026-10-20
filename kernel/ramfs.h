#ifndef RAMFS_H
#define RAMFS_H
#include "vfs.h"
extern const struct vfs_ops ramfs_ops;
void ramfs_init(void);
int ramfs_add(const char *, const void *, uint32_t);
int ramfs_self_test(void);
#endif
