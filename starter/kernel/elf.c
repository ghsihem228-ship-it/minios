#include "elf.h"
#include "bytes.h"
#define BASE UINT64_C(0x40000000)
#define LIMIT (BASE + 14 * 4096)
struct header {
    unsigned char id[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phsize, phnum, shsize, shnum, shstr;
};
struct phdr { uint32_t type, flags; uint64_t offset, va, pa, filesz, memsz, align; };
_Static_assert(sizeof(struct header) == 64, "ELF64 header");
_Static_assert(sizeof(struct phdr) == 56, "ELF64 program header");
int elf_validate(const void *data, size_t size, struct elf_plan *out) {
    struct header h;
    *out = (struct elf_plan){0};
    if (size < sizeof(h)) return 0;
    bytes_copy(&h, data, sizeof(h));
    if (!bytes_equal(h.id, "\177ELF\2\1\1", 7) || h.type != 2 || h.machine != 62 ||
        h.version != 1 || h.ehsize != 64 || h.phsize != 56 || !h.phnum || h.phnum > ELF_SEGMENTS ||
        h.phoff > size || h.phnum > (size - h.phoff) / 56) return 0;
    struct elf_plan plan = {.entry = h.entry}; int entry_ok = 0;
    for (unsigned i = 0; i < h.phnum; i++) {
        struct phdr p;
        bytes_copy(&p, (const unsigned char *)data + h.phoff + i * 56, sizeof(p));
        if (p.type == 0 || p.type == 4 || p.type == 6 || p.type == 0x6474e551) {
            if (p.type == 0x6474e551 && (p.flags & 1)) return 0;
            continue;
        }
        if (p.type != 1 || !p.memsz || p.va < BASE || p.va >= LIMIT || p.memsz > LIMIT - p.va ||
            p.filesz > p.memsz || p.offset > size || p.filesz > size - p.offset ||
            (p.flags & ~7u) || !(p.flags & 4) || ((p.flags & 3) == 3) ||
            (p.va & 4095) ||
            (p.align > 4096) || (p.align && (p.align & (p.align - 1))) ||
            (p.align > 1 && ((p.va - p.offset) & (p.align - 1)))) return 0;
        uint64_t end = (p.va + p.memsz + 4095) & ~UINT64_C(4095);
        for (unsigned j = 0; j < plan.count; j++) {
            struct elf_segment *s = &plan.segments[j];
            uint64_t prior_end = (s->address + s->memsz + 4095) & ~UINT64_C(4095);
            if (p.va < prior_end && s->address < end) return 0;
        }
        if ((p.flags & 1) && h.entry >= p.va && h.entry - p.va < p.filesz) entry_ok = 1;
        plan.segments[plan.count++] = (struct elf_segment){p.offset, p.va, p.filesz, p.memsz, p.flags};
    }
    if (!entry_ok) return 0;
    *out = plan; return 1;
}
