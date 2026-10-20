#include "fs.h"
#include "bytes.h"
static struct ramfs fixture;
int fs_selftest(void) {
    struct handle_table table; handle_t root, file, reader; size_t done; char data[8];
    handle_table_init(&table); fs_init(&fixture);
    if (fs_root(&fixture, &table, CAP_ALL, &root)) return 0;
    if (fs_open(&table, root, "../x", 4, CAP_READ, 0, &file) != SYS_EINVAL) return 0;
    if (fs_open(&table, root, "note", 4, CAP_READ | CAP_WRITE, 1, &file)) return 0;
    if (fs_write(&table, file, "abc", 3, &done) || done != 3) return 0;
    if (fs_seek(&table, file, 5) || fs_write(&table, file, "Z", 1, &done)) return 0;
    if (fs_open(&table, root, "note", 4, CAP_READ, 0, &reader)) return 0;
    if (fs_read(&table, reader, data, sizeof(data), &done) || done != 6 ||
        !bytes_equal(data, "abc\0\0Z", 6)) return 0;
    if (fs_write(&table, reader, "x", 1, &done) != SYS_EACCESS) return 0;
    if (fs_unlink(&table, root, "note", 4) != SYS_EBUSY) return 0;
    if (handle_close(&table, file) || handle_close(&table, reader)) return 0;
    if (fs_unlink(&table, root, "note", 4)) return 0;
    if (fs_open(&table, root, "note", 4, CAP_READ, 0, &file) != SYS_ENOENT) return 0;
    handle_table_clear(&table); return fixture.root.refs == 1;
}
