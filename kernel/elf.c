#include "elf.h"
#include "paging.h"
#include "pmm.h"

struct elf_header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));
struct program_header {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} __attribute__((packed));
_Static_assert(sizeof(struct elf_header) == 52, "ELF32 header");
_Static_assert(sizeof(struct program_header) == 32, "ELF32 program header");

int elf_load(unsigned slot, const uint8_t *image, uint32_t len, uint32_t *entry) {
    if (!image || len < sizeof(struct elf_header)) return -8;
    const struct elf_header *h = (const void *)image;
    if (h->ident[0] != 0x7F || h->ident[1] != 'E' || h->ident[2] != 'L' ||
        h->ident[3] != 'F' || h->ident[4] != 1 || h->ident[5] != 1 ||
        h->ident[6] != 1 || h->type != 2 || h->machine != 3 || h->version != 1 ||
        h->ehsize != sizeof(*h) || h->phentsize != sizeof(struct program_header) ||
        !h->phnum || h->phnum > 16 || h->phoff > len ||
        h->phnum > (len - h->phoff) / sizeof(struct program_header)) return -8;
    int executable_entry = 0, loaded = 0;
    /* Validate everything before allocating. PT_INTERP/PT_DYNAMIC unsupported. */
    for (unsigned i = 0; i < h->phnum; i++) {
        const struct program_header *p = (const void *)(image + h->phoff + i * h->phentsize);
        if (p->type == 2 || p->type == 3) return -8;
        if (p->type != 1 || !p->memsz) continue;
        if (p->vaddr < USER_BASE || p->vaddr >= USER_IMAGE_END ||
            p->memsz > USER_IMAGE_END - p->vaddr || p->filesz > p->memsz ||
            p->offset > len || p->filesz > len - p->offset ||
            ((p->flags & 3) == 3) || /* reject W+X load segments */
            (p->align > 1 && ((p->align & (p->align - 1)) ||
                ((p->vaddr - p->offset) & (p->align - 1))))) return -8;
        if ((p->flags & 1) && h->entry >= p->vaddr && h->entry - p->vaddr < p->memsz)
            executable_entry = 1;
        uint32_t start = p->vaddr & ~(PAGE_SIZE - 1);
        uint32_t end = (p->vaddr + p->memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (unsigned j = 0; j < i; j++) {
            const struct program_header *q = (const void *)(image + h->phoff + j * h->phentsize);
            if (q->type != 1 || !q->memsz) continue;
            uint32_t qs = q->vaddr & ~(PAGE_SIZE - 1);
            uint32_t qe = (q->vaddr + q->memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (start < qe && qs < end) return -8;
        }
        loaded++;
    }
    if (!loaded || !executable_entry) return -8;
    paging_space_init(slot);
    for (unsigned i = 0; i < h->phnum; i++) {
        const struct program_header *p = (const void *)(image + h->phoff + i * h->phentsize);
        if (p->type != 1 || !p->memsz) continue;
        uint32_t end = (p->vaddr + p->memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (uint32_t addr = p->vaddr & ~(PAGE_SIZE - 1); addr < end; addr += PAGE_SIZE)
            if (!paging_user_map(slot, addr, (p->flags & 2) != 0)) goto exhausted;
        if (p->filesz && !paging_space_write(slot, p->vaddr, image + p->offset, p->filesz))
            goto exhausted;
    }
    for (uint32_t addr = USER_STACK_BOTTOM; addr < USER_TOP; addr += PAGE_SIZE)
        if (!paging_user_map(slot, addr, 1)) goto exhausted;
    *entry = h->entry;
    return 0;
exhausted:
    paging_space_destroy(slot);
    return -12;
}

int elf_self_test(void) {
    /* A tiny valid image, then independent malformed variants. Also exercise
     * partial allocation rollback under the low-RAM QEMU configuration. */
    struct test_image {
        struct elf_header h;
        struct program_header p;
        uint8_t code;
    } __attribute__((packed)) image = {
        .h = { .ident = {0x7F, 'E', 'L', 'F', 1, 1, 1}, .type = 2, .machine = 3,
            .version = 1, .entry = USER_BASE, .phoff = sizeof(struct elf_header),
            .ehsize = sizeof(struct elf_header), .phentsize = sizeof(struct program_header),
            .phnum = 1 },
        .p = { .type = 1, .offset = sizeof(struct elf_header) + sizeof(struct program_header),
            .vaddr = USER_BASE, .filesz = 1, .memsz = 1, .flags = 5, .align = 1 },
        .code = 0xC3,
    };
    const unsigned slot = MAX_USER_SPACES - 1;
    uint32_t entry, before = pmm_free_frames();
    int ok = elf_load(slot, (const uint8_t *)&image, sizeof(image), &entry) == 0;
    ok &= entry == USER_BASE;
    paging_space_destroy(slot);
    ok &= pmm_free_frames() == before;
    image.p.flags = 7; /* W+X */
    ok &= elf_load(slot, (const uint8_t *)&image, sizeof(image), &entry) == -8;
    image.p.flags = 5; image.p.offset = UINT32_MAX;
    ok &= elf_load(slot, (const uint8_t *)&image, sizeof(image), &entry) == -8;
    image.p.offset = sizeof(image) - 1; image.p.vaddr = 0x100000;
    ok &= elf_load(slot, (const uint8_t *)&image, sizeof(image), &entry) == -8;
    image.p.vaddr = USER_BASE; image.h.entry = USER_TOP;
    ok &= elf_load(slot, (const uint8_t *)&image, sizeof(image), &entry) == -8;
    image.h.entry = USER_BASE; image.h.phoff = UINT32_MAX;
    ok &= elf_load(slot, (const uint8_t *)&image, sizeof(image), &entry) == -8;
    image.h.phoff = sizeof(struct elf_header);
    image.p.memsz = USER_IMAGE_END - USER_BASE;
    int status = elf_load(slot, (const uint8_t *)&image, sizeof(image), &entry);
    ok &= status == 0 || status == -12;
    if (!status) paging_space_destroy(slot);
    return ok && pmm_free_frames() == before;
}
