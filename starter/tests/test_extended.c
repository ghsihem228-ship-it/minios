#include "fs.h"
#include "elf.h"
#include "pipe.h"
#include "error.h"
#include "bytes.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static struct ramfs filesystem;
static uint32_t seed = 0x924731ab;
static uint32_t random_word(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static void transaction_test(void) {
    struct handle_table t; handle_t root, file; size_t done; char bytes[8];
    handle_table_init(&t); fs_init(&filesystem);
    assert(fs_root(&filesystem, &t, CAP_ALL, &root) == 0);
    handle_t fills[31], unused;
    for (unsigned i = 0; i < 31; i++) assert(handle_install(&t, &filesystem.root, CAP_READ, &fills[i]) == OBJ_OK);
    assert(fs_open(&t, root, "phantom", 7, CAP_READ, 1, &unused) == SYS_ENOSPC && !unused);
    assert(!filesystem.image.nodes[0].used);
    for (unsigned i = 0; i < 31; i++) assert(handle_close(&t, fills[i]) == OBJ_OK);
    assert(handle_clone(&t, root, 0, &unused) == OBJ_OK);
    handle_t forbidden;
    assert(fs_open(&t, unused, "file", 4, 0, 0, &forbidden) == SYS_EACCESS);
    assert(handle_close(&t, unused) == OBJ_OK);
    assert(fs_open(&t, root, "file", 4, CAP_READ | CAP_WRITE, 1, &file) == 0);
    assert(fs_write(&t, file, "before", 6, &done) == 0);
    assert(fs_seek(&t, file, 0) == 0 && fs_tx_begin(&filesystem));
    assert(fs_write(&t, file, "after!", 6, &done) == 0);
    assert(bytes_equal(filesystem.image.nodes[0].data, "before", 6));
    assert(fs_tx_end(&filesystem, 1));
    assert(fs_read(&t, file, bytes, 6, &done) == 0 && done == 6 && bytes_equal(bytes, "before", 6));
    assert(fs_seek(&t, file, 0) == 0 && fs_tx_begin(&filesystem));
    assert(fs_write(&t, file, "commit", 6, &done) == 0);
    assert(fs_tx_end(&filesystem, 0));
    assert(bytes_equal(filesystem.image.nodes[0].data, "commit", 6));
    assert(fs_tx_begin(&filesystem));
    filesystem.image.revision++; /* independent writer injection: conflict must not commit */
    assert(!fs_tx_end(&filesystem, 0) && fs_tx_end(&filesystem, 1));
    handle_table_clear(&t);
    assert(filesystem.root.refs == 1);
}
int main(int argc, char **argv) {
    assert(argc == 2 && fs_selftest() && pipe_selftest());
    transaction_test();
    struct error_chain error;
    error_record(&error, SYS_EACCESS, SYS_FS_WRITE, CAP_WRITE);
    assert(error.version == 1 && error.count == 3);
    assert(error_field(&error, 2, 2).value == CAP_WRITE);
    assert(error_field(&error, 4, 0).status == SYS_EINVAL && error.count == 3);
    error_record(&error, 0, 0, 0); assert(error.count == 0);
    FILE *input = fopen(argv[1], "rb"); assert(input);
    assert(fseek(input, 0, SEEK_END) == 0); long n = ftell(input); assert(n > 64);
    rewind(input); unsigned char *elf = malloc((size_t)n), *mutated = malloc((size_t)n);
    assert(elf && mutated && fread(elf, 1, (size_t)n, input) == (size_t)n); fclose(input);
    struct elf_plan plan; assert(elf_validate(elf, (size_t)n, &plan));
    unsigned char frame[PIPE_FRAME]; uint16_t type;
    const unsigned char bad_utf8[] = {1,0,4,0,2,0,0,0,0xc0,0x80};
    assert(!wire_validate(bad_utf8, sizeof(bad_utf8), &type));
    for (unsigned i = 0; i < 100000; i++) {
        bytes_copy(mutated, elf, (size_t)n);
        unsigned changes = 1 + random_word() % 6;
        while (changes--) mutated[random_word() % (size_t)n] = (unsigned char)random_word();
        size_t length = i & 1 ? (size_t)n : random_word() % (size_t)n;
        if (elf_validate(mutated, length, &plan)) {
            assert(plan.count > 0 && plan.count <= ELF_SEGMENTS);
            for (unsigned j = 0; j < plan.count; j++) {
                struct elf_segment *s = &plan.segments[j];
                assert(s->offset <= length && s->filesz <= length - s->offset);
                assert(s->address >= 0x40000000 && s->memsz <= 0x4000e000 - s->address);
                assert((s->flags & 3) != 3);
            }
        }
        length = random_word() % (PIPE_FRAME + 1);
        for (size_t j = 0; j < length; j++) frame[j] = (unsigned char)random_word();
        (void)wire_validate(frame, length, &type);
        /* Force a valid envelope so scalar/text/record validators also run. */
        if (length >= 8) {
            frame[0] = 1; frame[1] = 0; frame[2] = (unsigned char)(1 + i % 6); frame[3] = 0;
            frame[4] = (unsigned char)(length - 8); frame[5] = frame[6] = frame[7] = 0;
            (void)wire_validate(frame, length, &type);
        }
    }
    free(elf); free(mutated);
    puts("PASS extended host: error bounds, VFS/RAMFS, commit/undo/conflicts, pipes; 100000 ELF/wire mutants under ASan/UBSan");
    return 0;
}
