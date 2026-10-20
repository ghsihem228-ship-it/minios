#ifndef STARTER_VFS_H
#define STARTER_VFS_H
#include "handle.h"
#include "syscall_abi.h"
struct vfs_operations {
    int64_t (*read)(struct object *, void *, size_t, size_t *);
    int64_t (*write)(struct object *, const void *, size_t, size_t *);
    int64_t (*seek)(struct object *, uint64_t);
};
/* Every file backend embeds this prefix; ops are trusted kernel pointers. */
struct vfs_file { struct object object; const struct vfs_operations *operations; };
int64_t fs_read(struct handle_table *, handle_t, void *, size_t, size_t *);
int64_t fs_write(struct handle_table *, handle_t, const void *, size_t, size_t *);
int64_t fs_seek(struct handle_table *, handle_t, uint64_t);
#endif
