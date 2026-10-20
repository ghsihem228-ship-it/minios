#ifndef VFS_H
#define VFS_H
#include <stdint.h>
#define FS_PATH_MAX 64
#define FS_FILE_CAP 4096u
#define FS_WRITE 1u
#define FS_CREATE 2u
#define FS_TRUNC 4u
#define FS_EXCL 8u
struct file_handle;
struct fs_stat { uint32_t size; int readonly; };
struct vfs_ops {
    int (*open)(struct file_handle *, const char *, uint32_t);
    int (*read)(struct file_handle *, void *, uint32_t);
    int (*write)(struct file_handle *, const void *, uint32_t);
    int (*seek)(struct file_handle *, uint32_t);
    int (*close)(struct file_handle *);
    int (*unlink)(const char *);
    int (*stat)(const char *, struct fs_stat *);
    void (*list)(void);
};
struct file_handle {
    void *node;
    const struct vfs_ops *ops;
    uint32_t offset, flags;
};
void fs_init(void);
int fs_add(const char *path, const void *data, uint32_t len);
int fs_open(struct file_handle *f, const char *path, uint32_t flags);
int fs_read(struct file_handle *f, void *out, uint32_t len);
int fs_write(struct file_handle *f, const void *in, uint32_t len);
int fs_seek(struct file_handle *f, uint32_t offset);
int fs_close(struct file_handle *f);
int fs_unlink(const char *path);
int fs_stat(const char *path, struct fs_stat *out);
int fs_read_all(const char *path, uint8_t **image, uint32_t *len);
void fs_list(void);
int fs_list_path(const char *path);
int fs_sync(void);
void fs_info(void);
int fs_self_test(void);
#endif
