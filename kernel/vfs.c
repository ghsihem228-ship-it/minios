#include "vfs.h"
#include "ramfs.h"
#include "fat32.h"
#include "block.h"
#include "heap.h"
#include "vga.h"
#include <stddef.h>

static int path_valid(const char *p) {
    if (!p || *p++ != '/') return 0;
    unsigned length = 1, component = 0;
    for (; *p; p++, length++) {
        if (length >= FS_PATH_MAX - 1) return 0;
        if (*p == '/') { if (!component) return 0; component = 0; }
        else {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.')) return 0;
            if (*p == '.' && !component && (p[1] == '.' || p[1] == '/' || !p[1])) return 0;
            component++;
        }
    }
    return length == 1 || component != 0;
}
static const struct vfs_ops *route(const char **path) {
    const char *p = *path;
    if (p[0] == '/' && p[1] == 'd' && p[2] == 'i' && p[3] == 's' && p[4] == 'k' &&
        (!p[5] || p[5] == '/')) {
        *path += 5;
        return fat32_ready() ? &fat32_ops : NULL;
    }
    return &ramfs_ops;
}
void fs_init(void) {
    ramfs_init();
    int status = block_init();
    if (status) { vga_puts("ATA: no usable primary-master disk; RAMFS only\n"); return; }
    vga_puts("ATA: sectors="); vga_put_dec(block_sectors()); vga_putc('\n');
    status = fat32_mount();
    if (status) { vga_puts("FAT32: mount rejected errno="); vga_put_dec(-status); vga_putc('\n'); }
    else vga_puts("FAT32 mounted at /disk (use df for volume state)\n");
}
int fs_add(const char *path, const void *data, uint32_t len) {
    if (!path_valid(path)) return -22;
    const char *local = path;
    if (route(&local) != &ramfs_ops) return -30;
    return ramfs_add(path, data, len);
}
int fs_open(struct file_handle *f, const char *path, uint32_t flags) {
    if (!f || f->node || !path_valid(path) || (flags & ~15u) || ((flags & FS_EXCL) && !(flags & FS_CREATE)) ||
        ((flags & (FS_CREATE | FS_TRUNC)) && !(flags & FS_WRITE))) return -22;
    const struct vfs_ops *ops = route(&path);
    if (!ops) return -19;
    int error = ops->open(f, path, flags);
    if (!error) f->ops = ops;
    return error;
}
int fs_read(struct file_handle *f, void *out, uint32_t len) {
    return f && f->node && f->ops ? f->ops->read(f, out, len) : -9;
}
int fs_write(struct file_handle *f, const void *in, uint32_t len) {
    return f && f->node && f->ops ? f->ops->write(f, in, len) : -9;
}
int fs_seek(struct file_handle *f, uint32_t offset) {
    return f && f->node && f->ops ? f->ops->seek(f, offset) : -9;
}
int fs_close(struct file_handle *f) {
    if (!f || !f->node || !f->ops) return -9;
    int error = f->ops->close(f);
    f->node = NULL; f->ops = NULL; f->offset = f->flags = 0;
    return error;
}
int fs_unlink(const char *path) {
    if (!path_valid(path)) return -22;
    const struct vfs_ops *ops = route(&path);
    return ops ? ops->unlink(path) : -19;
}
int fs_stat(const char *path, struct fs_stat *out) {
    if (!path_valid(path) || !out) return -22;
    const struct vfs_ops *ops = route(&path);
    return ops ? ops->stat(path, out) : -19;
}
int fs_read_all(const char *path, uint8_t **image, uint32_t *len) {
    struct fs_stat stat;
    int error = fs_stat(path, &stat);
    if (error) return error;
    if (!stat.size) return -8;
    if (stat.size > 1024u * 1024u) return -7;
    struct file_handle f = {0};
    if ((error = fs_open(&f, path, 0))) return error;
    uint8_t *data = kmalloc(stat.size);
    if (!data) { fs_close(&f); return -12; }
    uint32_t read = 0;
    while (read < stat.size) {
        int got = fs_read(&f, data + read, stat.size - read);
        if (got <= 0) { error = got < 0 ? got : -5; break; }
        read += got;
    }
    fs_close(&f);
    if (error) { kfree(data); heap_trim(); return error; }
    *image = data; *len = read;
    return 0;
}
void fs_list(void) {
    ramfs_ops.list();
    vga_puts(fat32_ready() ? "mount /disk FAT32\n" : "/disk unavailable\n");
}
int fs_list_path(const char *path) {
    if (!path_valid(path)) return -22;
    if (path[0] == '/' && !path[1]) { fs_list(); return 0; }
    const struct vfs_ops *ops = route(&path);
    if (!ops) return -19;
    if (ops == &fat32_ops && (!*path || (*path == '/' && !path[1]))) { ops->list(); return 0; }
    return -20;
}
int fs_sync(void) { return fat32_sync(); }
void fs_info(void) {
    vga_puts("VFS: / RAMFS (volatile), /disk FAT32 (persistent)\n");
    fat32_info();
}
int fs_self_test(void) { return ramfs_self_test(); }
