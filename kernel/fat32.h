#ifndef FAT32_H
#define FAT32_H
#include "vfs.h"
extern const struct vfs_ops fat32_ops;
int fat32_mount(void);
int fat32_ready(void);
int fat32_sync(void);
void fat32_info(void);
#endif
