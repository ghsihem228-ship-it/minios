#include "heap.h"
#include "paging.h"
#include "pmm.h"

#define FREE_MAGIC 0xF4EEB10Cu
#define USED_MAGIC 0xA110CA7Eu
#define ALIGN 16u

struct block {
    uint32_t size, magic;
    struct block *prev, *next;
};
_Static_assert(sizeof(struct block) == ALIGN, "heap header must be 16 bytes");

static struct block *first;
static uint32_t committed;

/* Map all new pages before publishing a new block. On failure, roll back
 * every frame acquired by this growth attempt, leaving the list intact. */
static int grow(uint32_t bytes) {
    if (bytes > HEAP_MAX_BYTES - committed)
        return 0;
    bytes = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (bytes > HEAP_MAX_BYTES - committed)
        return 0;
    uint32_t added = 0;
    while (added < bytes) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame)
            break;
        if (!paging_map_page(HEAP_BASE + committed + added, frame)) {
            pmm_free_frame(frame);
            break;
        }
        added += PAGE_SIZE;
    }
    if (added != bytes) {
        while (added) {
            uint32_t frame;
            added -= PAGE_SIZE;
            uint32_t virt = HEAP_BASE + committed + added;
            if (paging_translate(virt, &frame)) {
                paging_unmap_page(virt);
                pmm_free_frame(frame);
            }
        }
        return 0;
    }
    struct block *last = first;
    while (last && last->next)
        last = last->next;
    if (last && last->magic == FREE_MAGIC) {
        last->size += bytes;
    } else {
        struct block *b = (struct block *)(HEAP_BASE + committed);
        b->size = bytes - sizeof(*b);
        b->magic = FREE_MAGIC;
        b->prev = last;
        b->next = NULL;
        if (last) last->next = b;
        else first = b;
    }
    committed += bytes;
    return 1;
}

int heap_init(void) {
    if (first)
        return 1;
    return paging_enabled() && grow(PAGE_SIZE);
}

void *kmalloc(size_t size) {
    if (!first || !size || size > HEAP_MAX_BYTES - sizeof(struct block))
        return NULL;
    uint32_t need = (size + ALIGN - 1) & ~(ALIGN - 1);
    for (;;) {
        struct block *last = NULL;
        for (struct block *b = first; b; b = b->next) {
            last = b;
            if (b->magic != FREE_MAGIC || b->size < need)
                continue;
            if (b->size - need >= sizeof(*b) + ALIGN) {
                struct block *tail = (struct block *)((char *)(b + 1) + need);
                tail->size = b->size - need - sizeof(*b);
                tail->magic = FREE_MAGIC;
                tail->prev = b;
                tail->next = b->next;
                if (tail->next) tail->next->prev = tail;
                b->next = tail;
                b->size = need;
            }
            b->magic = USED_MAGIC;
            return b + 1;
        }
        uint32_t extra = need + sizeof(struct block);
        if (last && last->magic == FREE_MAGIC)
            extra = need - last->size;
        if (!grow(extra))
            return NULL;
    }
}

static void merge_next(struct block *b) {
    struct block *next = b->next;
    b->size += sizeof(*next) + next->size;
    b->next = next->next;
    if (b->next) b->next->prev = b;
}

void kfree(void *ptr) {
    if (!ptr)
        return;
    /* Search by payload address. Do not dereference an untrusted pointer. */
    for (struct block *b = first; b; b = b->next) {
        if ((void *)(b + 1) != ptr)
            continue;
        if (b->magic != USED_MAGIC)
            return;
        b->magic = FREE_MAGIC;
        if (b->next && b->next->magic == FREE_MAGIC)
            merge_next(b);
        if (b->prev && b->prev->magic == FREE_MAGIC)
            merge_next(b->prev);
        return;
    }
}

int heap_validate(void) {
    if (!first || !committed || committed > HEAP_MAX_BYTES)
        return 0;
    uint32_t end = HEAP_BASE + committed, expected = HEAP_BASE;
    struct block *prev = NULL;
    for (struct block *b = first; b; b = b->next) {
        uint32_t addr = (uint32_t)b;
        if (addr != expected || (addr & (ALIGN - 1)) ||
            addr > end - sizeof(*b))
            return 0;
        if ((b->magic != USED_MAGIC && b->magic != FREE_MAGIC) ||
            b->prev != prev || b->size < ALIGN || (b->size & (ALIGN - 1)) ||
            b->size > end - addr - sizeof(*b))
            return 0;
        if (prev && prev->magic == FREE_MAGIC && b->magic == FREE_MAGIC)
            return 0;
        expected = addr + sizeof(*b) + b->size;
        if ((b->next && (uint32_t)b->next != expected) ||
            (!b->next && expected != end))
            return 0;
        prev = b;
    }
    return expected == end;
}

struct heap_stats heap_get_stats(void) {
    struct heap_stats s = { .committed = committed };
    for (struct block *b = first; b; b = b->next) {
        s.blocks++;
        if (b->magic == USED_MAGIC) s.used += b->size;
        else {
            s.free += b->size;
            if (b->size > s.largest_free) s.largest_free = b->size;
        }
    }
    return s;
}

int heap_self_test(void) {
    if (!heap_validate() || heap_get_stats().used)
        return 0;
    uint32_t frames_before = pmm_free_frames(), bytes_before = committed;
    void *a = kmalloc(32), *b = kmalloc(64), *c = kmalloc(32);
    if (!a || !b || !c) {
        kfree(a); kfree(b); kfree(c);
        return 0;
    }
    kfree(b); kfree(a);
    void *merged = kmalloc(96);
    int ok = merged == a;
    kfree(merged); kfree(c);
    uint8_t *large = kmalloc(9001);
    if (!large) return 0;
    ok &= !((uint32_t)large & (ALIGN - 1));
    for (uint32_t i = 0; i < 9001; i++) large[i] = (uint8_t)(i * 37);
    for (uint32_t i = 0; i < 9001; i++)
        if (large[i] != (uint8_t)(i * 37)) ok = 0;
    uint32_t used = heap_get_stats().used;
    kfree(large + 1); kfree((void *)1); kfree(NULL);
    ok &= heap_get_stats().used == used;
    kfree(large); kfree(large);
    ok &= !kmalloc(0) && !kmalloc((size_t)-1) && !kmalloc(HEAP_MAX_BYTES);
    /* Exercise bounded growth/exhaustion and recovery on small RAM too. */
    void *chunks[64];
    uint32_t n = 0;
    while (n < 64 && (chunks[n] = kmalloc(65536)) != NULL) {
        ((uint8_t *)chunks[n])[0] = (uint8_t)n;
        ((uint8_t *)chunks[n])[65535] = (uint8_t)(n ^ 0xAA);
        n++;
    }
    ok &= n < 64;
    for (uint32_t i = 0; i < n; i++) {
        ok &= ((uint8_t *)chunks[i])[0] == (uint8_t)i;
        ok &= ((uint8_t *)chunks[i])[65535] == (uint8_t)(i ^ 0xAA);
    }
    while (n) kfree(chunks[--n]);
    struct heap_stats s = heap_get_stats();
    return ok && heap_validate() && !s.used && s.blocks == 1 &&
        frames_before - pmm_free_frames() == (committed - bytes_before) / PAGE_SIZE &&
        s.free + sizeof(struct block) == s.committed;
}
