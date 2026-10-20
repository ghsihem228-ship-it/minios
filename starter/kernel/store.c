#include "store.h"
#include "block.h"
#include "bytes.h"
#define BANK_SECTORS 64u
static unsigned ready, selected;
static uint64_t sequence;
static unsigned char payload[STORE_BYTES];
static struct fs_image loaded;
static uint16_t sector_words[256];
static unsigned char *const sector = (unsigned char *)sector_words;
static uint32_t read32(const unsigned char *p) { return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t read64(const unsigned char *p) { return read32(p) | ((uint64_t)read32(p+4)<<32); }
static void write32(unsigned char *p, uint32_t v) { for (unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(8*i)); }
static void write64(unsigned char *p, uint64_t v) { for (unsigned i=0;i<8;i++) p[i]=(unsigned char)(v>>(8*i)); }
int store_ready(void) { return ready; }
uint64_t store_sequence(void) { return sequence; }
int64_t store_init(struct ramfs *fs) {
    ready = 0; sequence = 0; selected = 1;
    if (block_init() || block_sectors() < 129 || block_read(0, sector)) return SYS_ENOENT;
    if (!bytes_equal(sector, "MOS64DSK", 8) || read32(sector+8)!=1 ||
        read32(sector+12)!=BANK_SECTORS || read32(sector+16)!=2) return SYS_EFORMAT;
    unsigned nonempty = 0;
    for (unsigned bank = 0; bank < 2; bank++) {
        uint32_t base = 1 + bank * BANK_SECTORS;
        if (block_read(base, sector)) return SYS_EIO;
        for (unsigned i = 0; i < 512; i++) if (sector[i]) { nonempty = 1; break; }
        uint64_t candidate = read64(sector + 16);
        if (!bytes_equal(sector, "MOS64FS1", 8) || read32(sector+8)!=1 ||
            read32(sector+12)!=STORE_BYTES || !candidate || read32(sector+28)!=store_hash(sector,28)) continue;
        uint32_t checksum = read32(sector+24);
        for (unsigned i = 0; i < STORE_SECTORS; i++) {
            if (block_read(base + 1 + i, sector)) return SYS_EIO;
            size_t offset = i * 512u, size = STORE_BYTES - offset;
            if (size > 512) size = 512;
            bytes_copy(payload + offset, sector, size);
        }
        if (store_hash(payload, STORE_BYTES) != checksum || candidate <= sequence) continue;
        if (!store_decode(payload, STORE_BYTES, &loaded)) continue;
        sequence = candidate; selected = bank;
    }
    if (!sequence && nonempty) return SYS_EFORMAT; /* Do not overwrite unrecoverable data. */
    if (sequence) { loaded.revision = sequence; fs->image = loaded; }
    ready = 1; return SYS_OK;
}
int64_t store_save(struct ramfs *fs) {
    if (!ready) return SYS_EACCESS;
    if (fs->transaction) return SYS_EBUSY;
    if (sequence == UINT64_MAX) return SYS_EOVERFLOW;
    store_encode(&fs->image, payload);
    unsigned bank = selected ^ 1; uint32_t base = 1 + bank * BANK_SECTORS;
    for (unsigned i = 0; i < STORE_SECTORS; i++) {
        bytes_zero(sector, 512);
        size_t offset = i * 512u, size = STORE_BYTES - offset;
        if (size > 512) size = 512;
        bytes_copy(sector, payload + offset, size);
        if (block_write(base + 1 + i, sector)) goto failed;
    }
    if (block_flush()) goto failed;
    bytes_zero(sector, 512); bytes_copy(sector, "MOS64FS1", 8);
    write32(sector+8, 1); write32(sector+12, STORE_BYTES); write64(sector+16, sequence+1);
    write32(sector+24, store_hash(payload, STORE_BYTES)); write32(sector+28, store_hash(sector,28));
    if (block_write(base, sector) || block_flush()) goto failed;
    selected = bank; sequence++; return SYS_OK;
failed:
    ready = 0; return SYS_EIO; /* Stop writes after an I/O failure. */
}
