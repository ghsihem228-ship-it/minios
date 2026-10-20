#include "fs.h"
#include "bytes.h"
#include <stddef.h>
#define ROOT_FS(o) ((struct ramfs *)((char *)(o) - offsetof(struct ramfs, root)))
static struct fs_image *view(struct ramfs *fs) { return fs->transaction && fs->dirty ? &fs->overlay : &fs->image; }
static void prepare(struct ramfs *fs) {
    if (fs->transaction && !fs->dirty) { fs->overlay = fs->image; fs->dirty = 1; }
}
int fs_tx_begin(struct ramfs *fs) {
    if (fs->transaction) return 0;
    fs->transaction = 1; fs->dirty = 0; fs->baseline = fs->image.revision;
    for (unsigned i = 0; i < FS_OPENS; i++) fs->offsets[i] = fs->opens[i].offset;
    return 1;
}
int fs_tx_end(struct ramfs *fs, int rollback) {
    if (!fs->transaction) return 0;
    if (!rollback && fs->image.revision != fs->baseline) return 0;
    if (!rollback && fs->dirty) fs->image = fs->overlay;
    if (rollback) for (unsigned i = 0; i < FS_OPENS; i++) fs->opens[i].offset = fs->offsets[i];
    fs->transaction = fs->dirty = 0; return 1;
}
static int64_t status(enum object_status s) {
    return s == OBJ_OK ? SYS_OK : s == OBJ_DENIED ? SYS_EACCESS :
           s == OBJ_FULL ? SYS_ENOSPC : s == OBJ_OVERFLOW ? SYS_EOVERFLOW : SYS_EBADHANDLE;
}
static const struct vfs_operations ram_operations;
static void root_destroy(struct object *o) { (void)o; } /* fs storage is permanent */
static void file_destroy(struct object *o) { ((struct fs_open_file *)o)->used = 0; }
void fs_init(struct ramfs *fs) {
    bytes_zero(fs, sizeof(*fs));
    (void)object_init(&fs->root, OBJECT_DIRECTORY, root_destroy);
}
int64_t fs_root(struct ramfs *fs, struct handle_table *t, cap_rights_t rights, handle_t *out) {
    return status(handle_install(t, &fs->root, rights, out));
}
static int valid_name(const char *s, size_t n) {
    if (!n || n > FS_NAME || (n == 1 && s[0] == '.') ||
        (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-' || s[i] == '.')) return 0;
    return 1;
}
static int node_find(struct ramfs *fs, const char *s, size_t n) {
    for (unsigned i = 0; i < FS_FILES; i++) {
        struct fs_node *node = &view(fs)->nodes[i];
        if (node->used && node->name[n] == 0 && bytes_equal(node->name, s, n)) return (int)i;
    }
    return -1;
}
int64_t fs_open(struct handle_table *t, handle_t dir, const char *name, size_t length,
                cap_rights_t rights, int create, handle_t *out) {
    *out = 0;
    if (!valid_name(name, length) || (rights & ~(CAP_READ | CAP_WRITE | CAP_DUPLICATE | CAP_TRANSFER)))
        return SYS_EINVAL;
    struct object *root;
    enum object_status s = handle_lookup(t, dir, OBJECT_DIRECTORY, CAP_READ | rights | (create ? CAP_WRITE : 0), &root);
    if (s != OBJ_OK) return status(s);
    struct ramfs *fs = ROOT_FS(root);
    if (fs->transaction) { object_put(root); return SYS_EBUSY; }
    int index = node_find(fs, name, length), fresh = index < 0;
    int64_t result = SYS_OK;
    if (fresh && !create) { result = SYS_ENOENT; goto done; }
    if (fresh) {
        for (index = 0; index < (int)FS_FILES && view(fs)->nodes[index].used; index++) {}
        if (index == (int)FS_FILES) { result = SYS_ENOSPC; goto done; }
    }
    unsigned slot;
    for (slot = 0; slot < FS_OPENS && fs->opens[slot].used; slot++) {}
    if (slot == FS_OPENS) { result = SYS_ENOSPC; goto done; }
    struct fs_open_file *file = &fs->opens[slot];
    file->base.operations = &ram_operations;
    file->used = 1; file->fs = fs; file->node = (unsigned)index; file->offset = 0;
    (void)object_init(&file->base.object, OBJECT_FILE, file_destroy);
    result = status(handle_install(t, &file->base.object, rights, out));
    object_put(&file->base.object);
    if (result == SYS_OK && fresh) {
        struct fs_node *node = &view(fs)->nodes[index];
        bytes_zero(node, sizeof(*node));
        bytes_copy(node->name, name, length); node->used = 1;
        view(fs)->revision++;
    }
done:
    object_put(root); return result;
}
static int64_t ram_read(struct object *o, void *data, size_t length, size_t *done) {
    struct fs_open_file *file = (struct fs_open_file *)o;
    struct fs_node *node = &view(file->fs)->nodes[file->node];
    size_t n = file->offset < node->size ? node->size - file->offset : 0;
    if (n > length) n = length;
    bytes_copy(data, node->data + file->offset, n); file->offset += (unsigned)n;
    *done = n; return SYS_OK;
}
static int64_t ram_write(struct object *o, const void *data, size_t length, size_t *done) {
    struct fs_open_file *file = (struct fs_open_file *)o;
    if (length > FS_BYTES - file->offset) return SYS_ENOSPC;
    prepare(file->fs);
    struct fs_node *node = &view(file->fs)->nodes[file->node];
    if (file->offset > node->size) bytes_zero(node->data + node->size, file->offset - node->size);
    bytes_copy(node->data + file->offset, data, length);
    if (length) {
        file->offset += (unsigned)length;
        if (node->size < file->offset) node->size = file->offset;
        view(file->fs)->revision++;
    }
    *done = length; return SYS_OK;
}
static int64_t ram_seek(struct object *o, uint64_t offset) {
    if (offset > FS_BYTES) return SYS_EINVAL;
    ((struct fs_open_file *)o)->offset = (unsigned)offset;
    return SYS_OK;
}
static const struct vfs_operations ram_operations = {ram_read, ram_write, ram_seek};
int64_t fs_unlink(struct handle_table *t, handle_t dir, const char *name, size_t length) {
    if (!valid_name(name, length)) return SYS_EINVAL;
    struct object *o;
    enum object_status s = handle_lookup(t, dir, OBJECT_DIRECTORY, CAP_WRITE, &o);
    if (s != OBJ_OK) return status(s);
    struct ramfs *fs = ROOT_FS(o);
    if (fs->transaction) { object_put(o); return SYS_EBUSY; }
    int index = node_find(fs, name, length); int64_t result = SYS_OK;
    if (index < 0) result = SYS_ENOENT;
    else {
        for (unsigned i = 0; i < FS_OPENS; i++)
            if (fs->opens[i].used && fs->opens[i].node == (unsigned)index) { result = SYS_EBUSY; break; }
        if (result == SYS_OK) { bytes_zero(&view(fs)->nodes[index], sizeof(struct fs_node)); view(fs)->revision++; }
    }
    object_put(o); return result;
}
