/* FAT32, 512-byte sectors, mirrored FATs, root-directory 8.3 files.
 * Write-through transactions mark the volume dirty until FLUSH CACHE succeeds.
 * This detects interrupted operations; it is not a journal or atomic overwrite.
 */
#include "fat32.h"
#include "block.h"
#include "vga.h"
#include <stddef.h>

#define EOC 0x0FFFFFFFu
#define CLEAN (1u << 27)
#define HARD_OK (1u << 26)
#define MASK 0x0FFFFFFFu
#define FILE_MAX (16u * 1024u * 1024u)
#define OPEN_NODES 32
#define ROOT_LIMIT 256u
struct volume {
    uint32_t start, total, fat, fat_size, data, clusters, root, spc, fats;
    uint32_t hint, info, backup_info, free;
    int mounted, readonly;
};
static struct volume v;
static uint8_t cache[2][512];
static uint32_t cached[2];
static int cache_valid[2];
static uint32_t root_clusters[ROOT_LIMIT], root_count;
struct location {
    uint32_t sector, offset, first, size;
    uint8_t attr;
    int lfn, end;
};
struct fat_node { struct location entry; uint32_t refs; };
static struct fat_node nodes[OPEN_NODES];
struct walk { uint32_t c, anchor, power, length, hops; };

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p + 2) << 16); }
static void p16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void p32(uint8_t *p, uint32_t n) { p16(p, n); p16(p + 2, n >> 16); }
static int bad(void) { v.readonly = 1; cache_valid[0] = cache_valid[1] = 0; return -5; }
static int rd(uint32_t lba, void *buf) { return block_read(lba, buf) ? bad() : 0; }
static int wr(uint32_t lba, const void *buf) { return block_write(lba, buf) ? bad() : 0; }
static int flush(void) { return block_flush() ? bad() : 0; }
static int cluster(uint32_t c) { return c >= 2 && c - 2 < v.clusters; }
static uint32_t data_sector(uint32_t c) { return v.data + (c - 2) * v.spc; }

static int fat_get(uint32_t c, uint32_t *value) {
    if (c > v.clusters + 1) return bad();
    uint32_t sector = c / 128, offset = (c % 128) * 4;
    for (unsigned i = 0; i < v.fats; i++) {
        if (!cache_valid[i] || cached[i] != sector) {
            if (rd(v.fat + i * v.fat_size + sector, cache[i])) return -5;
            cached[i] = sector; cache_valid[i] = 1;
        }
    }
    *value = u32(cache[0] + offset) & MASK;
    if (v.fats == 2 && *value != (u32(cache[1] + offset) & MASK)) return bad();
    return 0;
}
static int fat_set(uint32_t c, uint32_t value) {
    uint32_t old;
    if (fat_get(c, &old)) return -5;
    unsigned offset = (c % 128) * 4;
    for (unsigned i = 0; i < v.fats; i++) {
        uint32_t upper = u32(cache[i] + offset) & ~MASK;
        p32(cache[i] + offset, upper | (value & MASK));
        if (wr(v.fat + i * v.fat_size + c / 128, cache[i])) return -5;
    }
    if (c >= 2 && !old && value) v.free--;
    if (c >= 2 && old && !value) v.free++;
    return 0;
}
static struct walk walking(uint32_t c) {
    struct walk w = { .c = c, .anchor = c, .power = 1 };
    return w;
}
/* Brent cycle detection, with an additional geometry-derived hop bound. */
static int advance(struct walk *w) {
    uint32_t next;
    if (!cluster(w->c) || fat_get(w->c, &next)) return bad();
    if (next >= 0x0FFFFFF8u) { w->c = 0; return 0; }
    if (!cluster(next) || ++w->hops > v.clusters) return bad();
    w->c = next;
    if (++w->length && w->c == w->anchor) return bad();
    if (w->length == w->power) {
        w->anchor = w->c; w->length = 0; w->power *= 2;
    }
    return 0;
}
static int chain_count(uint32_t first, uint32_t *count, uint32_t *tail) {
    *count = *tail = 0;
    if (!first) return 0;
    struct walk w = walking(first);
    while (w.c) {
        if (!cluster(w.c)) return bad();
        *tail = w.c; (*count)++;
        if (advance(&w)) return -5;
    }
    return 0;
}
static int file_chain(uint32_t first, uint32_t *count, uint32_t *tail) {
    *count = *tail = 0;
    struct walk w = walking(first);
    while (w.c) {
        for (uint32_t i = 0; i < root_count; i++) if (root_clusters[i] == w.c) return bad();
        *tail = w.c; (*count)++;
        if (advance(&w)) return -5;
    }
    return 0;
}
static int free_chain(uint32_t first) {
    struct walk w = walking(first);
    while (w.c) {
        uint32_t c = w.c;
        if (advance(&w) || fat_set(c, 0)) return -5;
        if (c < v.hint) v.hint = c;
    }
    return 0;
}
static int allocate(uint32_t *out) {
    if (!v.free) return -28;
    for (uint32_t n = 0; n < v.clusters; n++) {
        uint32_t c = 2 + (v.hint - 2 + n) % v.clusters, entry;
        if (fat_get(c, &entry)) return -5;
        if (entry) continue;
        if (fat_set(c, EOC)) return -5;
        uint8_t zero[512] = {0};
        for (uint32_t s = 0; s < v.spc; s++)
            if (wr(data_sector(c) + s, zero)) return -5;
        v.hint = c == v.clusters + 1 ? 2 : c + 1;
        *out = c; return 0;
    }
    return -28;
}
static int reserve_chain(uint32_t count, uint32_t *head) {
    uint32_t tail = 0;
    *head = 0;
    while (count--) {
        uint32_t c;
        int error = allocate(&c);
        if (error) {
            if (error == -28 && *head && free_chain(*head)) return -5;
            *head = 0; return error;
        }
        if (tail && fat_set(tail, c)) return -5;
        if (!*head) *head = c;
        tail = c;
    }
    return 0;
}
static int update_info(uint32_t lba, int known) {
    if (!lba) return 0;
    uint8_t b[512];
    if (rd(lba, b)) return -5;
    if (u32(b) != 0x41615252 || u32(b + 484) != 0x61417272 ||
        u32(b + 508) != 0xAA550000) return 0;
    p32(b + 488, known ? v.free : UINT32_MAX);
    p32(b + 492, known ? v.hint : UINT32_MAX);
    return wr(lba, b);
}
static int begin(void) {
    if (!v.mounted) return -19;
    if (v.readonly) return -30;
    uint32_t flags;
    if (fat_get(1, &flags) || fat_set(1, flags & ~CLEAN) || flush()) return -5;
    if (update_info(v.info, 0) || update_info(v.backup_info, 0)) return -5;
    return 0;
}
static int finish(int result) {
    if (v.readonly) return -5;
    uint32_t flags;
    if (flush() || update_info(v.info, 1) || update_info(v.backup_info, 1) ||
        flush() || fat_get(1, &flags) || fat_set(1, flags | CLEAN) || flush()) return -5;
    return result;
}
static int name83(const char *path, uint8_t name[11]) {
    if (*path == '/') path++;
    if (!*path) return -21;
    for (unsigned i = 0; i < 11; i++) name[i] = ' ';
    unsigned base = 0, ext = 0;
    int dot = 0;
    for (; *path; path++) {
        unsigned char c = *path;
        if (c == '.') { if (dot || !base) return -22; dot = 1; continue; }
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return -22;
        if (!dot) { if (base == 8) return -36; name[base++] = c; }
        else { if (ext == 3) return -36; name[8 + ext++] = c; }
    }
    return !base || (dot && !ext) ? -22 : 0;
}
static int lookup(const uint8_t name[11], struct location *found,
                  struct location *empty, uint32_t *tail) {
    struct walk w = walking(v.root);
    int lfn = 0;
    if (empty) empty->sector = 0;
    while (w.c) {
        *tail = w.c;
        for (uint32_t s = 0; s < v.spc; s++) {
            uint8_t b[512];
            uint32_t lba = data_sector(w.c) + s;
            if (rd(lba, b)) return -5;
            for (unsigned p = 0; p < 512; p += 32) {
                if (!b[p] || b[p] == 0xE5) {
                    if (empty && !empty->sector) {
                        empty->sector = lba; empty->offset = p; empty->end = !b[p];
                    }
                    lfn = 0;
                    if (!b[p]) return -2;
                    continue;
                }
                if (b[p + 11] == 0x0F) { lfn = 1; continue; }
                int match = 1;
                for (unsigned i = 0; i < 11; i++) if (b[p + i] != name[i]) match = 0;
                if (match && !(b[p + 11] & 8)) {
                    found->sector = lba; found->offset = p;
                    found->attr = b[p + 11]; found->lfn = lfn;
                    found->end = 0;
                    found->first = ((uint32_t)u16(b + p + 20) << 16) | u16(b + p + 26);
                    found->size = u32(b + p + 28);
                    if ((found->first && !cluster(found->first)) || (found->size && !found->first)) return bad();
                    return 0;
                }
                lfn = 0;
            }
        }
        if (advance(&w)) return -5;
    }
    return -2;
}
static int publish(const struct location *e) {
    uint8_t b[512];
    if (rd(e->sector, b)) return -5;
    p16(b + e->offset + 20, e->first >> 16);
    p16(b + e->offset + 26, e->first);
    p32(b + e->offset + 28, e->size);
    b[e->offset + 11] |= 0x20;
    return wr(e->sector, b);
}
static int new_entry(const uint8_t name[11], struct location *empty, uint32_t tail) {
    if (!empty->sector) {
        if (root_count == ROOT_LIMIT) return -28;
        uint32_t c;
        int error = allocate(&c);
        if (error) return error;
        if (fat_set(tail, c)) return -5;
        root_clusters[root_count++] = c;
        empty->sector = data_sector(c); empty->offset = 0; empty->end = 1;
    }
    /* Preserve the terminating 0x00 after consuming an end marker, even if
     * foreign media contain stale directory bytes beyond that marker. */
    if (empty->end && empty->offset == 480) {
        struct walk w = walking(v.root);
        uint32_t next_sector = 0;
        while (w.c) {
            uint32_t start = data_sector(w.c);
            if (empty->sector >= start && empty->sector - start < v.spc) {
                if (empty->sector - start + 1 < v.spc) next_sector = empty->sector + 1;
                else {
                    if (advance(&w)) return -5;
                    if (w.c) next_sector = data_sector(w.c);
                }
                break;
            }
            if (advance(&w)) return -5;
        }
        if (next_sector) {
            uint8_t next[512];
            if (rd(next_sector, next)) return -5;
            next[0] = 0;
            if (wr(next_sector, next)) return -5;
        }
    }
    uint8_t b[512];
    if (rd(empty->sector, b)) return -5;
    if (empty->end && empty->offset < 480) b[empty->offset + 32] = 0;
    for (unsigned i = 0; i < 32; i++) b[empty->offset + i] = 0;
    for (unsigned i = 0; i < 11; i++) b[empty->offset + i] = name[i];
    b[empty->offset + 11] = 0x20;
    p16(b + empty->offset + 16, 0x21); /* 1980-01-01, no RTC yet */
    p16(b + empty->offset + 18, 0x21); p16(b + empty->offset + 24, 0x21);
    if (wr(empty->sector, b)) return -5;
    empty->first = empty->size = 0; empty->attr = 0x20; empty->lfn = 0;
    return 0;
}
static struct fat_node *active(const struct location *e) {
    for (unsigned i = 0; i < OPEN_NODES; i++)
        if (nodes[i].refs && nodes[i].entry.sector == e->sector && nodes[i].entry.offset == e->offset)
            return &nodes[i];
    return NULL;
}
static int fat_open(struct file_handle *f, const char *path, uint32_t flags) {
    if ((flags & FS_WRITE) && v.readonly) return -30;
    uint8_t name[11];
    int error = name83(path, name);
    if (error) return error;
    struct location entry, empty;
    uint32_t tail = v.root;
    error = lookup(name, &entry, &empty, &tail);
    if (!error && (flags & FS_EXCL)) return -17;
    if (error && error != -2) return error;
    struct fat_node *n = error ? NULL : active(&entry);
    if (!n) for (unsigned i = 0; i < OPEN_NODES; i++)
        if (!nodes[i].refs) { n = &nodes[i]; break; }
    if (!n) return -24;
    if (error) {
        if (!(flags & FS_CREATE)) return -2;
        if ((error = begin())) return error;
        error = new_entry(name, &empty, tail);
        error = finish(error);
        if (error) return error;
        entry = empty;
    }
    if (entry.attr & 0x10) return -21;
    if (entry.size > FILE_MAX) return -27;
    uint32_t chain_length, chain_tail;
    if (file_chain(entry.first, &chain_length, &chain_tail)) return -5;
    if (chain_length < (entry.size + v.spc * 512 - 1) / (v.spc * 512)) return bad();
    if ((flags & FS_WRITE) && (v.readonly || (entry.attr & 1))) return -30;
    if ((flags & FS_WRITE) && entry.lfn) return -95;
    if (!n->refs) n->entry = entry;
    if ((flags & FS_TRUNC) && n->entry.first) {
        uint32_t count, last, old = n->entry.first;
        if (file_chain(old, &count, &last)) return -5;
        if ((error = begin())) return error;
        struct location truncated = n->entry;
        truncated.first = truncated.size = 0;
        if (publish(&truncated)) return -5;
        n->entry = truncated;
        if (free_chain(old)) return -5;
        if ((error = finish(0))) return error;
    }
    n->refs++; f->node = n; f->flags = flags; f->offset = 0;
    return 0;
}
static int position(uint32_t first, uint32_t offset, struct walk *w) {
    *w = walking(first);
    uint32_t skip = offset / (v.spc * 512);
    while (skip--) {
        if (!w->c || advance(w)) return bad();
    }
    return cluster(w->c) ? 0 : bad();
}
static int fat_read(struct file_handle *f, void *out, uint32_t len) {
    struct fat_node *n = f->node;
    if (!len || f->offset >= n->entry.size) return 0;
    if (len > n->entry.size - f->offset) len = n->entry.size - f->offset;
    struct walk w;
    if (position(n->entry.first, f->offset, &w)) return -5;
    uint8_t *to = out;
    uint32_t done = 0, at = f->offset;
    while (done < len) {
        uint8_t b[512];
        uint32_t offset = at % 512, bytes = 512 - offset;
        if (bytes > len - done) bytes = len - done;
        if (rd(data_sector(w.c) + (at / 512) % v.spc, b)) return -5;
        for (uint32_t i = 0; i < bytes; i++) to[done + i] = b[offset + i];
        at += bytes; done += bytes;
        if (done < len && at % (v.spc * 512) == 0)
            if (advance(&w) || !w.c) return bad();
    }
    f->offset += done; return done;
}
static int write_range(uint32_t first, uint32_t at, const uint8_t *src, uint32_t len) {
    if (!len) return 0;
    struct walk w;
    if (position(first, at, &w)) return -5;
    while (len) {
        uint8_t b[512];
        uint32_t offset = at % 512, bytes = 512 - offset;
        if (bytes > len) bytes = len;
        uint32_t sector = data_sector(w.c) + (at / 512) % v.spc;
        if ((offset || bytes != 512) && rd(sector, b)) return -5;
        for (uint32_t i = 0; i < bytes; i++) b[offset + i] = src ? src[i] : 0;
        if (wr(sector, b)) return -5;
        at += bytes; len -= bytes;
        if (src) src += bytes;
        if (len && at % (v.spc * 512) == 0)
            if (advance(&w) || !w.c) return bad();
    }
    return 0;
}
static int fat_write(struct file_handle *f, const void *in, uint32_t len) {
    struct fat_node *n = f->node;
    if (!(f->flags & FS_WRITE)) return -9;
    if (!len) return 0;
    if (v.readonly) return -30;
    if (f->offset > FILE_MAX || len > FILE_MAX - f->offset) return -27;
    uint32_t size = f->offset + len;
    if (size < n->entry.size) size = n->entry.size;
    uint32_t count, tail;
    if (file_chain(n->entry.first, &count, &tail)) return -5;
    uint32_t cb = v.spc * 512, needed = (size + cb - 1) / cb;
    if (count < (n->entry.size + cb - 1) / cb) return bad();
    int error = begin();
    if (error) return error;
    uint32_t extra = 0;
    if (needed > count) {
        error = reserve_chain(needed - count, &extra);
        if (error) return finish(error);
        if (tail && fat_set(tail, extra)) return -5;
        if (!n->entry.first) n->entry.first = extra;
    }
    if (f->offset > n->entry.size &&
        write_range(n->entry.first, n->entry.size, NULL, f->offset - n->entry.size)) return -5;
    if (write_range(n->entry.first, f->offset, in, len) || flush()) return -5;
    n->entry.size = size;
    if (publish(&n->entry)) return -5;
    error = finish(0);
    if (error) return error;
    f->offset += len;
    return len;
}
static int fat_seek(struct file_handle *f, uint32_t offset) {
    struct fat_node *n = f->node;
    if (offset > ((f->flags & FS_WRITE) ? FILE_MAX : n->entry.size)) return -22;
    f->offset = offset; return offset;
}
static int fat_close(struct file_handle *f) {
    struct fat_node *n = f->node;
    n->refs--; f->node = NULL; return 0;
}
static int fat_unlink(const char *path) {
    if (v.readonly) return -30;
    uint8_t name[11], b[512];
    int error = name83(path, name);
    if (error) return error;
    struct location e; uint32_t tail;
    if ((error = lookup(name, &e, NULL, &tail))) return error;
    if (e.attr & 0x10) return -21;
    if (e.attr & 1) return -30;
    if (e.lfn) return -95;
    if (active(&e)) return -16;
    uint32_t count, last;
    if (file_chain(e.first, &count, &last)) return -5;
    if ((error = begin())) return error;
    if (rd(e.sector, b)) return -5;
    b[e.offset] = 0xE5;
    if (wr(e.sector, b) || flush() || free_chain(e.first)) return -5;
    return finish(0);
}
static int fat_stat(const char *path, struct fs_stat *out) {
    uint8_t name[11];
    int error = name83(path, name);
    if (error) return error;
    struct location e; uint32_t tail;
    if ((error = lookup(name, &e, NULL, &tail))) return error;
    if (e.attr & 0x10) return -21;
    out->size = e.size; out->readonly = v.readonly || (e.attr & 1) || e.lfn;
    return 0;
}
static void fat_list(void) {
    struct walk w = walking(v.root);
    while (w.c) {
        for (uint32_t s = 0; s < v.spc; s++) {
            uint8_t b[512];
            if (rd(data_sector(w.c) + s, b)) { vga_puts("Disk read error.\n"); return; }
            for (unsigned p = 0; p < 512; p += 32) {
                if (!b[p]) return;
                if (b[p] == 0xE5 || (b[p + 11] & 8)) continue;
                vga_puts(b[p + 11] & 0x10 ? "dir " : "file ");
                for (unsigned i = 0; i < 8 && b[p + i] != ' '; i++) vga_putc(b[p + i]);
                if (b[p + 8] != ' ') {
                    vga_putc('.');
                    for (unsigned i = 8; i < 11 && b[p + i] != ' '; i++) vga_putc(b[p + i]);
                }
                vga_puts(" bytes="); vga_put_dec(u32(b + p + 28)); vga_putc('\n');
            }
        }
        if (advance(&w)) { vga_puts("Corrupt directory chain.\n"); return; }
    }
}
int fat32_mount(void) {
    uint8_t b[512];
    v.mounted = v.readonly = 0; cache_valid[0] = cache_valid[1] = 0;
    if (!block_sectors() || rd(0, b)) return -19;
    uint32_t start = 0, partition_size = block_sectors();
    if (u16(b + 11) != 512) {
        if (u16(b + 510) != 0xAA55) return -22;
        int found = 0;
        for (unsigned i = 0; i < 4; i++) {
            unsigned p = 446 + i * 16;
            if (b[p + 4] == 0x0B || b[p + 4] == 0x0C) {
                if (found++) return -95; /* ambiguous multiple FAT volumes */
                start = u32(b + p + 8); partition_size = u32(b + p + 12);
            }
        }
        if (!found || !start || start >= block_sectors() ||
            !partition_size || partition_size > block_sectors() - start || rd(start, b)) return -22;
    }
    uint32_t total = u32(b + 32), reserved = u16(b + 14), fats = b[16];
    uint32_t fat_size = u32(b + 36), spc = b[13];
    if (u16(b + 510) != 0xAA55 || u16(b + 11) != 512 || !spc || spc > 64 ||
        (spc & (spc - 1)) || !reserved || (fats != 1 && fats != 2) ||
        u16(b + 17) || u16(b + 19) || u16(b + 22) || u16(b + 42) ||
        (u16(b + 40) & 0x80) || !total || total > partition_size ||
        !fat_size || fat_size > total / fats || reserved >= total - fat_size * fats) return -22;
    uint32_t data_offset = reserved + fats * fat_size;
    uint32_t clusters = (total - data_offset) / spc;
    if (clusters < 65525 || clusters > 1048576 || fat_size < (clusters + 129) / 128) return -22;
    v.start = start; v.total = total; v.fat = start + reserved; v.fat_size = fat_size;
    v.data = start + data_offset; v.clusters = clusters; v.fats = fats; v.spc = spc;
    v.root = u32(b + 44); v.hint = 3; v.info = v.backup_info = 0;
    if (!cluster(v.root)) return -22;
    uint32_t info = u16(b + 48), backup = u16(b + 50);
    if (info && info < reserved) v.info = start + info;
    if (info && backup && backup < reserved && info < reserved - backup) v.backup_info = start + backup + info;
    uint32_t flags, media;
    if (fat_get(0, &media) || (media & 255) != b[21] ||
        fat_get(1, &flags) || (flags & 0x03FFFFFFu) != 0x03FFFFFFu) return -5;
    uint32_t count, tail;
    if (chain_count(v.root, &count, &tail)) return -5;
    if (count > ROOT_LIMIT) return -95;
    root_count = 0;
    struct walk root_walk = walking(v.root);
    while (root_walk.c) {
        root_clusters[root_count++] = root_walk.c;
        if (advance(&root_walk)) return -5;
    }
    if (!(flags & CLEAN) || !(flags & HARD_OK)) v.readonly = 1;
    v.free = 0;
    for (uint32_t c = 2; c < v.clusters + 2; c++) {
        uint32_t entry;
        if (fat_get(c, &entry)) return -5;
        if (!entry) v.free++;
    }
    v.mounted = 1;
    return 0;
}
int fat32_ready(void) { return v.mounted; }
int fat32_sync(void) {
    if (!v.mounted) return -19;
    if (v.readonly) return -30;
    return flush();
}
void fat32_info(void) {
    if (!v.mounted) { vga_puts("/disk: not mounted\n"); return; }
    vga_puts("/disk: FAT32 "); vga_puts(v.readonly ? "read-only" : "read-write");
    vga_puts(" clusters="); vga_put_dec(v.clusters);
    vga_puts(" cluster-bytes="); vga_put_dec(v.spc * 512);
    uint32_t free = 0, entry;
    for (uint32_t c = 2; c < v.clusters + 2; c++) {
        if (fat_get(c, &entry)) { vga_puts(" FAT error\n"); return; }
        if (!entry) free++;
    }
    vga_puts(" free-clusters="); vga_put_dec(free); vga_putc('\n');
}
const struct vfs_ops fat32_ops = {
    fat_open, fat_read, fat_write, fat_seek, fat_close, fat_unlink, fat_stat, fat_list
};
