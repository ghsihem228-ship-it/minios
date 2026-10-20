#ifndef STARTER_FS_H
#define STARTER_FS_H
#include "syscall_abi.h"
#include "handle.h"
#include "vfs.h"
#define FS_FILES 8u
#define FS_BYTES 2048u
#define FS_NAME 31u
#define FS_OPENS 16u
struct fs_node { unsigned used, size; char name[FS_NAME + 1]; unsigned char data[FS_BYTES]; };
struct fs_image { uint64_t revision; struct fs_node nodes[FS_FILES]; };
struct ramfs;
struct fs_open_file { struct vfs_file base; struct ramfs *fs; unsigned node, offset, used; };
struct ramfs { struct object root; struct fs_image image, overlay; int transaction, dirty;
uint64_t baseline; unsigned offsets[FS_OPENS]; struct fs_open_file opens[FS_OPENS]; };
void fs_init(struct ramfs *); /* fresh, persistently stored kernel object */
int64_t fs_root(struct ramfs *, struct handle_table *, cap_rights_t, handle_t *);
int64_t fs_open(struct handle_table *, handle_t directory, const char *, size_t length,
                cap_rights_t requested, int create, handle_t *);
int64_t fs_read(struct handle_table *, handle_t, void *, size_t, size_t *);
int64_t fs_write(struct handle_table *, handle_t, const void *, size_t, size_t *);
int64_t fs_seek(struct handle_table *, handle_t, uint64_t offset);
int64_t fs_unlink(struct handle_table *, handle_t directory, const char *, size_t);
int fs_tx_begin(struct ramfs *);
int fs_tx_end(struct ramfs *, int rollback);
int fs_selftest(void);
#endif
