#include "store.h"
#include "bytes.h"
static uint32_t read32(const unsigned char *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write32(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8*i));
}
uint32_t store_hash(const void *data, size_t size) {
    const unsigned char *p = data; uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; i++) hash = (hash ^ p[i]) * 16777619u;
    return hash; /* Accidental-corruption checksum, not authentication. */
}
static int name_valid(const unsigned char *s) {
    unsigned n = 0;
    while (n < 32 && s[n]) {
        if (!((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') ||
              (s[n] >= '0' && s[n] <= '9') || s[n] == '.' || s[n] == '_' || s[n] == '-')) return 0;
        n++;
    }
    if (!n || n == 32 || (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
    for (unsigned i = n; i < 32; i++) if (s[i]) return 0;
    return 1;
}
int store_decode(const void *data, size_t length, struct fs_image *out) {
    if (length != STORE_BYTES) return 0;
    const unsigned char *p = data;
    /* Validate the complete namespace before touching the destination. */
    for (unsigned i = 0; i < FS_FILES; i++) {
        const unsigned char *node = p + i * STORE_ENTRY;
        uint32_t used = read32(node), size = read32(node + 4);
        if (used > 1 || size > FS_BYTES || (!used && size)) return 0;
        if (used) {
            if (!name_valid(node + 8)) return 0;
            for (unsigned j = 0; j < i; j++) {
                const unsigned char *other = p + j * STORE_ENTRY;
                if (read32(other) && bytes_equal(node + 8, other + 8, 32)) return 0;
            }
        }
    }
    bytes_zero(out, sizeof(*out));
    for (unsigned i = 0; i < FS_FILES; i++) {
        const unsigned char *node = p + i * STORE_ENTRY;
        if (!read32(node)) continue;
        out->nodes[i].used = 1; out->nodes[i].size = read32(node + 4);
        bytes_copy(out->nodes[i].name, node + 8, 32);
        bytes_copy(out->nodes[i].data, node + 40, out->nodes[i].size);
    }
    return 1;
}
void store_encode(const struct fs_image *image, void *data) {
    unsigned char *p = data; bytes_zero(p, STORE_BYTES);
    for (unsigned i = 0; i < FS_FILES; i++) {
        const struct fs_node *node = &image->nodes[i];
        if (!node->used) continue;
        unsigned char *wire = p + i * STORE_ENTRY;
        write32(wire, 1); write32(wire + 4, node->size);
        bytes_copy(wire + 8, node->name, 32); bytes_copy(wire + 40, node->data, node->size);
    }
}
