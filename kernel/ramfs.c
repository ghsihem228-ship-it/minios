/* A bounded RAM filesystem with an absolute-path, flat namespace.
 * Kernel paths run to completion: no task switch inside these functions. */
#include "ramfs.h"
#include "vga.h"
#include <stddef.h>

#define FS_NODES 16
struct fs_node {
    char path[FS_PATH_MAX];
    const uint8_t *data;
    uint32_t size, opens;
    int used, readonly;
};
static struct fs_node nodes[FS_NODES];
static uint8_t storage[FS_NODES][FS_FILE_CAP];

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int valid(const char *p) {
    if (*p++ != '/' || !*p) return 0;
    unsigned n = 1, component = 0;
    for (; *p; p++, n++) {
        if (n >= FS_PATH_MAX - 1) return 0;
        if (*p == '/') {
            if (!component) return 0;
            component = 0;
        } else {
            /* No dot components or ambiguous traversal paths. */
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
                return 0;
            if (*p == '.' && (!component) && (p[1] == '/' || !p[1] || p[1] == '.'))
                return 0;
            component++;
        }
    }
    return component != 0;
}
static struct fs_node *find(const char *path) {
    for (unsigned i = 0; i < FS_NODES; i++)
        if (nodes[i].used && equal(nodes[i].path, path)) return &nodes[i];
    return NULL;
}
static struct fs_node *create(const char *path) {
    for (unsigned i = 0; i < FS_NODES; i++) {
        struct fs_node *n = &nodes[i];
        if (n->used) continue;
        unsigned j = 0;
        do { n->path[j] = path[j]; } while (path[j++]);
        n->data = storage[i]; n->size = 0; n->opens = 0;
        n->readonly = 0; n->used = 1;
        return n;
    }
    return NULL;
}
void ramfs_init(void) {
    static const char motd[] = "Welcome to MiniOS v0.8: processes, persistent storage and IPv4 networking.\n";
    ramfs_add("/etc/motd", motd, sizeof(motd) - 1);
}
int ramfs_add(const char *path, const void *data, uint32_t len) {
    if (!valid(path) || find(path)) return -22;
    struct fs_node *n = create(path);
    if (!n) return -28;
    n->data = data; n->size = len; n->readonly = 1;
    return 0;
}
int ramfs_open(struct file_handle *f, const char *path, uint32_t flags) {
    if (f->node || !valid(path) || (flags & ~15u) || ((flags & FS_EXCL) && !(flags & FS_CREATE)) ||
        ((flags & (FS_CREATE | FS_TRUNC)) && !(flags & FS_WRITE))) return -22;
    struct fs_node *n = find(path);
    if (n && (flags & FS_EXCL)) return -17;
    if (!n && (flags & FS_CREATE)) n = create(path);
    if (!n) return (flags & FS_CREATE) ? -28 : -2;
    if (n->readonly && (flags & FS_WRITE)) return -30;
    if (flags & FS_TRUNC) n->size = 0;
    f->node = n; f->offset = 0; f->flags = flags; n->opens++;
    return 0;
}
int ramfs_read(struct file_handle *f, void *out, uint32_t len) {
    struct fs_node *n = f->node;
    if (!f->node) return -9;
    if (f->offset >= n->size) return 0;
    uint32_t bytes = n->size - f->offset;
    if (len < bytes) bytes = len;
    uint8_t *p = out;
    for (uint32_t i = 0; i < bytes; i++) p[i] = n->data[f->offset + i];
    f->offset += bytes;
    return bytes;
}
int ramfs_write(struct file_handle *f, const void *in, uint32_t len) {
    struct fs_node *n = f->node;
    if (!f->node || !(f->flags & FS_WRITE)) return -9;
    if (!len) return 0;
    if (f->offset > FS_FILE_CAP || len > FS_FILE_CAP - f->offset) return -28;
    uint8_t *data = (uint8_t *)n->data;
    while (n->size < f->offset) data[n->size++] = 0;
    const uint8_t *p = in;
    for (uint32_t i = 0; i < len; i++) data[f->offset + i] = p[i];
    f->offset += len;
    if (f->offset > n->size) n->size = f->offset;
    return len;
}
int ramfs_seek(struct file_handle *f, uint32_t offset) {
    struct fs_node *n = f->node;
    if (!f->node) return -9;
    if (offset > ((f->flags & FS_WRITE) ? FS_FILE_CAP : n->size)) return -22;
    f->offset = offset; return offset;
}
static int ramfs_close(struct file_handle *f) {
    struct fs_node *n = f->node;
    if (f->node) n->opens--;
    f->node = NULL; f->offset = f->flags = 0; return 0;
}
int ramfs_unlink(const char *path) {
    struct fs_node *n = find(path);
    if (!n) return -2;
    if (n->readonly) return -30;
    if (n->opens) return -16;
    n->used = 0; return 0;
}
static int ramfs_stat(const char *path, struct fs_stat *out) {
    struct fs_node *n = find(path);
    if (!n) return -2;
    out->size = n->size; out->readonly = n->readonly; return 0;
}
void ramfs_list(void) {
    for (unsigned i = 0; i < FS_NODES; i++) if (nodes[i].used) {
        vga_puts(nodes[i].readonly ? "ro " : "rw ");
        vga_put_dec(nodes[i].size); vga_puts(" bytes ");
        vga_puts(nodes[i].path); vga_putc('\n');
    }
}
int ramfs_self_test(void) {
    struct file_handle a = {0}, b = {0};
    char out[4]; int ok = 1;
    if (ramfs_open(&a, "/tmp/selftest", FS_WRITE | FS_CREATE | FS_TRUNC)) return 0;
    ok &= ramfs_write(&a, "abc", 3) == 3 && ramfs_seek(&a, 0) == 0;
    ok &= ramfs_open(&b, "/tmp/selftest", 0) == 0;
    ok &= ramfs_read(&b, out, sizeof(out)) == 3 && out[0] == 'a' && out[2] == 'c';
    ok &= ramfs_read(&b, out, 1) == 0 && ramfs_unlink("/tmp/selftest") == -16;
    ok &= ramfs_seek(&a, FS_FILE_CAP) == FS_FILE_CAP && ramfs_write(&a, "a", 1) == -28;
    ramfs_close(&a); ramfs_close(&b);
    ok &= ramfs_unlink("/tmp/selftest") == 0;
    ok &= ramfs_open(&a, "/etc/motd", FS_WRITE) == -30;
    return ok;
}

const struct vfs_ops ramfs_ops = {
    ramfs_open, ramfs_read, ramfs_write, ramfs_seek, ramfs_close,
    ramfs_unlink, ramfs_stat, ramfs_list
};
