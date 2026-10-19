#ifndef RAMFS_H
#define RAMFS_H
#include <stdint.h>

#define FS_PATH_MAX 64
#define FS_FILE_CAP 4096u
#define FS_WRITE 1u
#define FS_CREATE 2u
#define FS_TRUNC 4u
struct fs_node;
struct file_handle {
    struct fs_node *node;
    uint32_t offset, flags;
};
void fs_init(void);
int fs_add(const char *path, const void *data, uint32_t len);
int fs_open(struct file_handle *f, const char *path, uint32_t flags);
int fs_read(struct file_handle *f, void *out, uint32_t len);
int fs_write(struct file_handle *f, const void *in, uint32_t len);
int fs_seek(struct file_handle *f, uint32_t offset);
void fs_close(struct file_handle *f);
int fs_unlink(const char *path);
const uint8_t *fs_image(const char *path, uint32_t *len);
void fs_list(void);
int fs_self_test(void);
#endif
