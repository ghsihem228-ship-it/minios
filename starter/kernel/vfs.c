#include "vfs.h"
static int64_t status(enum object_status s) {
    return s == OBJ_DENIED ? SYS_EACCESS : s == OBJ_OVERFLOW ? SYS_EOVERFLOW : SYS_EBADHANDLE;
}
int64_t fs_read(struct handle_table *t, handle_t h, void *data, size_t length, size_t *done) {
    *done = 0; struct object *o;
    enum object_status s = handle_lookup(t, h, OBJECT_FILE, CAP_READ, &o);
    if (s != OBJ_OK) return status(s);
    struct vfs_file *f = (struct vfs_file *)o;
    int64_t result = f->operations->read(o, data, length, done);
    object_put(o); return result;
}
int64_t fs_write(struct handle_table *t, handle_t h, const void *data, size_t length, size_t *done) {
    *done = 0; struct object *o;
    enum object_status s = handle_lookup(t, h, OBJECT_FILE, CAP_WRITE, &o);
    if (s != OBJ_OK) return status(s);
    struct vfs_file *f = (struct vfs_file *)o;
    int64_t result = f->operations->write(o, data, length, done);
    object_put(o); return result;
}
int64_t fs_seek(struct handle_table *t, handle_t h, uint64_t offset) {
    struct object *o;
    enum object_status s = handle_lookup(t, h, OBJECT_FILE, 0, &o);
    if (s != OBJ_OK) return status(s);
    struct vfs_file *f = (struct vfs_file *)o;
    int64_t result = f->operations->seek(o, offset);
    object_put(o); return result;
}
