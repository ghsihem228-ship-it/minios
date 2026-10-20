#include "vmm.h"

#define ADDRESS_MASK UINT64_C(0x000ffffffffff000)
extern const char __load_start[], __bss_end[], __text_start[], __text_end[];
extern const char __syscall_guard[];
extern const char __rodata_start[], __rodata_end[];
extern const char __boot_guard[], __df_guard[], __nmi_guard[], __mc_guard[];
static uint64_t root_physical;
static uint64_t *arena_pt;
static int active;
/* Retained normalized input, not pointers into reclaimable loader memory. */
static struct boot_memory boot_map;
static struct phys_range reserved[BOOT_MAX_MODULES + 4];

static uint64_t *table(uint64_t physical) { return (uint64_t *)(uintptr_t)physical; }
static void zero_frame(uint64_t physical) {
    uint64_t *p = table(physical);
    for (unsigned n = 0; n < 512; n++) p[n] = 0;
}
static uint64_t new_table(void) {
    uint64_t physical = pmm_alloc_table();
    if (physical) zero_frame(physical);
    return physical;
}
/* Build-time only. Runtime arena tables are preallocated, making mapping
 * failures atomic without an intermediate-table allocation rollback path. */
static uint64_t *leaf(uint64_t va, int create) {
    uint64_t *p = table(root_physical);
    for (unsigned shift = 39; shift > 12; shift -= 9) {
        unsigned index = (va >> shift) & 511;
        if (!(p[index] & PTE_PRESENT)) {
            if (!create) return 0;
            uint64_t next = new_table();
            if (!next) return 0;
            p[index] = next | PTE_PRESENT | PTE_WRITE;
        }
        if (p[index] & 0x80) return 0; /* This manager does not mix huge pages. */
        p = table(p[index] & ADDRESS_MASK);
    }
    return &p[(va >> 12) & 511];
}
static int guard(uint64_t pa) {
    return pa == (uintptr_t)__syscall_guard || pa == (uintptr_t)__boot_guard || pa == (uintptr_t)__df_guard ||
           pa == (uintptr_t)__nmi_guard || pa == (uintptr_t)__mc_guard;
}
static int identity(uint64_t pa, uint64_t flags) {
    uint64_t *pte = leaf(pa, 1);
    if (!pte) return 0;
    *pte = pa | flags | PTE_PRESENT;
    return 1;
}
int memory_init(uint32_t boot_info) {
    unsigned count;
    if (active) return 0;
    if (!bootmem_capture(boot_info, &boot_map, reserved, &count)) return 0;
    reserved[count++] = (struct phys_range){(uintptr_t)__load_start,
                                          (uintptr_t)__bss_end - (uintptr_t)__load_start};
    if (!pmm_init(&boot_map, reserved, count)) return 0;
    root_physical = new_table();
    if (!root_physical) return 0;
    /* All manageable free/owned RAM is direct mapped RW/NX. Firmware holes,
     * loader data and modules remain reserved and are not mapped by default. */
    for (uint64_t pa = PAGE_SIZE; pa < (uint64_t)pmm_frame_count() * PAGE_SIZE; pa += PAGE_SIZE)
        if (pmm_state(pa) != FRAME_RESERVED && !identity(pa, PTE_WRITE | PTE_NX)) return 0;
    for (uint64_t pa = (uintptr_t)__load_start; pa < (uintptr_t)__bss_end; pa += PAGE_SIZE) {
        if (guard(pa)) continue;
        uint64_t flags = PTE_WRITE | PTE_NX;
        if (pa >= (uintptr_t)__text_start && pa < (uintptr_t)__text_end) flags = 0;
        if (pa >= (uintptr_t)__rodata_start && pa < (uintptr_t)__rodata_end) flags = PTE_NX;
        if (!identity(pa, flags)) return 0;
    }
    /* QEMU PC VGA text aperture: explicit supervisor RW/NX MMIO mapping. */
    if (!identity(0xb8000, PTE_WRITE | PTE_NX)) return 0;
    uint64_t *slot = leaf(VM_ARENA, 1);
    if (!slot) return 0;
    arena_pt = slot; /* Arena starts at a 2-MiB boundary: index 0 in its PT. */
    /* NXE was checked/enabled in bootstrap. WP also enforces RO in ring 0. */
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %0, %%cr3" :: "r"(root_physical) : "memory");
    cr0 |= UINT64_C(1) << 16;
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
    active = 1;
    return 1;
}
static int arena_index(uint64_t va, unsigned *index) {
    if (!active || va < VM_ARENA || va - VM_ARENA >= VM_ARENA_PAGES * PAGE_SIZE || va % PAGE_SIZE) return 0;
    *index = (va - VM_ARENA) / PAGE_SIZE;
    return 1;
}
static void invalidate(uint64_t va) {
    __asm__ volatile ("invlpg (%0)" :: "r"(va) : "memory");
}
int vm_map_page(uint64_t va, uint64_t physical) {
    unsigned index;
    if (!arena_index(va, &index) || arena_pt[index] & PTE_PRESENT ||
        !pmm_set_mapped(physical, 1)) return 0;
    arena_pt[index] = physical | PTE_PRESENT | PTE_WRITE | PTE_NX;
    invalidate(va);
    return 1;
}
int vm_unmap_page(uint64_t va, uint64_t *physical) {
    unsigned index;
    if (!physical || !arena_index(va, &index) || !(arena_pt[index] & PTE_PRESENT)) return 0;
    uint64_t pa = arena_pt[index] & ADDRESS_MASK;
    if (pmm_state(pa) != FRAME_MAPPED) return 0;
    arena_pt[index] = 0;
    invalidate(va); /* Invalidate before ownership returns to the allocator. */
    if (!pmm_set_mapped(pa, 0)) return 0;
    *physical = pa;
    return 1;
}
int vm_alloc_page(uint64_t va) {
    unsigned index;
    if (!arena_index(va, &index) || arena_pt[index] & PTE_PRESENT) return 0;
    uint64_t pa = pmm_alloc();
    if (!pa) return 0;
    zero_frame(pa); /* A caller never sees data left by a previous owner. */
    if (vm_map_page(va, pa)) return 1;
    (void)pmm_free(pa);
    return 0;
}
int vm_free_page(uint64_t va) {
    uint64_t pa;
    return vm_unmap_page(va, &pa) && pmm_free(pa);
}
int vm_lookup(uint64_t va, uint64_t *physical, uint64_t *flags) {
    uint64_t high = va >> 47;
    if (!active || (high != 0 && high != 0x1ffff) || !physical || !flags) return 0;
    uint64_t *pte = leaf(va, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return 0;
    *physical = (*pte & ADDRESS_MASK) | (va & (PAGE_SIZE - 1));
    *flags = *pte & ~ADDRESS_MASK;
    return 1;
}

#define REQUIRE(condition) do { if (!(condition)) return 0; } while (0)
int memory_selftest(void) {
    uint64_t baseline = pmm_free_count(), pa, flags;
    REQUIRE(!vm_lookup(0, &pa, &flags));
    REQUIRE(!vm_lookup((uintptr_t)__boot_guard, &pa, &flags));
    REQUIRE(!vm_lookup((uintptr_t)__df_guard, &pa, &flags));
    REQUIRE(!vm_lookup((uintptr_t)__nmi_guard, &pa, &flags));
    REQUIRE(!vm_lookup((uintptr_t)__mc_guard, &pa, &flags));
    REQUIRE(vm_lookup((uintptr_t)__text_start, &pa, &flags) && !(flags & (PTE_WRITE | PTE_NX)));
    REQUIRE(vm_lookup((uintptr_t)__rodata_start, &pa, &flags) && !(flags & PTE_WRITE) && (flags & PTE_NX));
    REQUIRE(!pmm_free(0) && !pmm_free(root_physical) && !pmm_free((uintptr_t)__text_start));
    uint64_t first = pmm_alloc(), second = pmm_alloc();
    REQUIRE(first && second && first != second && !pmm_free(first + 1));
    REQUIRE(vm_map_page(VM_ARENA, first) && !pmm_free(first));
    REQUIRE(!vm_map_page(VM_ARENA + PAGE_SIZE, first));
    REQUIRE(!vm_map_page(VM_ARENA, second));
    volatile uint64_t *alias = (volatile uint64_t *)(uintptr_t)VM_ARENA;
    *alias = UINT64_C(0x1020304050607080);
    REQUIRE(*(volatile uint64_t *)(uintptr_t)first == *alias);
    REQUIRE(vm_unmap_page(VM_ARENA, &pa) && pa == first);
    REQUIRE(!vm_unmap_page(VM_ARENA, &pa));
    REQUIRE(vm_map_page(VM_ARENA, second));
    *alias = UINT64_C(0x8877665544332211);
    REQUIRE(*(volatile uint64_t *)(uintptr_t)second == *alias);
    REQUIRE(*(volatile uint64_t *)(uintptr_t)first == UINT64_C(0x1020304050607080));
    REQUIRE(vm_free_page(VM_ARENA) && pmm_free(first) && !pmm_free(first));
    REQUIRE(!vm_alloc_page(0) && !vm_alloc_page(VM_ARENA + 1));
    REQUIRE(!vm_alloc_page(VM_ARENA + VM_ARENA_PAGES * PAGE_SIZE));
    REQUIRE(!vm_lookup(UINT64_C(0x0000800000000000), &pa, &flags));
    REQUIRE(!vm_map_page(VM_ARENA, root_physical));
    REQUIRE(!vm_map_page(VM_ARENA, (uintptr_t)__text_start));
    REQUIRE(vm_alloc_page(VM_ARENA));
    for (unsigned n = 0; n < PAGE_SIZE / sizeof(uint64_t); n++) REQUIRE(!alias[n]);
    REQUIRE(!vm_unmap_page(VM_ARENA, 0));
    REQUIRE(vm_free_page(VM_ARENA) && pmm_free_count() == baseline);
    uint64_t last = VM_ARENA + (VM_ARENA_PAGES - 1) * PAGE_SIZE;
    REQUIRE(vm_alloc_page(last) && vm_lookup(last + 7, &pa, &flags) && pa % PAGE_SIZE == 7);
    REQUIRE(vm_free_page(last) && pmm_free_count() == baseline);
    /* Exhaustion is exercised once at boot, before there are other owners. */
    uint64_t allocations = 0;
    while (pmm_alloc()) allocations++;
    REQUIRE(allocations == baseline && !pmm_alloc());
    REQUIRE(!vm_alloc_page(VM_ARENA));
    /* With exactly one frame freed, zero-fill must erase that frame's old data. */
    uint64_t reused = PAGE_SIZE;
    while (reused < (uint64_t)pmm_frame_count() * PAGE_SIZE && pmm_state(reused) != FRAME_OWNED)
        reused += PAGE_SIZE;
    REQUIRE(pmm_state(reused) == FRAME_OWNED);
    for (unsigned n = 0; n < 512; n++) table(reused)[n] = UINT64_C(0xaabbccdd11223344);
    REQUIRE(pmm_free(reused) && pmm_free_count() == 1);
    REQUIRE(vm_alloc_page(VM_ARENA));
    REQUIRE(vm_lookup(VM_ARENA, &pa, &flags) && pa == reused);
    for (unsigned n = 0; n < 512; n++) REQUIRE(!alias[n]);
    REQUIRE(vm_free_page(VM_ARENA));
    for (uint64_t p = 0; p < (uint64_t)pmm_frame_count() * PAGE_SIZE; p += PAGE_SIZE)
        if (pmm_state(p) == FRAME_OWNED) REQUIRE(pmm_free(p));
    REQUIRE(pmm_free_count() == baseline);
    /* Leave one zeroed arena page for external table inspection / NX tests. */
    REQUIRE(vm_alloc_page(VM_ARENA));
    REQUIRE(vm_lookup(VM_ARENA, &pa, &flags) && (flags & PTE_NX) && (flags & PTE_WRITE));
    return 1;
}

/* One bounded user arena. Shared kernel mappings stay supervisor-only at leaves.
 * Parent U bits alone do not grant access to supervisor kernel leaves. */
#include "user_vm.h"
#include "bytes.h"
static uint64_t *user_pt;
static uint64_t saved_user[USER_PAGES];
static int snapshot;
static int user_tables(void) {
    if (user_pt) return 1;
    uint64_t *slot = leaf(USER_BASE, 1);
    if (!slot) return 0;
    uint64_t *p = table(root_physical);
    for (unsigned shift = 39; shift > 12; shift -= 9) {
        unsigned index = (USER_BASE >> shift) & 511;
        p[index] |= PTE_USER;
        p = table(p[index] & ADDRESS_MASK);
    }
    user_pt = slot;
    return 1;
}
int vm_user_map(unsigned index, uint64_t flags) {
    if (index >= USER_PAGES || (flags & ~(PTE_WRITE | PTE_NX)) ||
        ((flags & PTE_WRITE) && !(flags & PTE_NX)) || !user_tables() || user_pt[index]) return 0;
    uint64_t pa = pmm_alloc();
    if (!pa) return 0;
    zero_frame(pa);
    if (!pmm_set_mapped(pa, 1)) { (void)pmm_free(pa); return 0; }
    user_pt[index] = pa | flags | PTE_USER | PTE_PRESENT;
    invalidate(USER_BASE + index * PAGE_SIZE); return 1;
}
void *vm_user_physical(unsigned index) {
    if (!user_pt || index >= USER_PAGES || !(user_pt[index] & PTE_PRESENT)) return NULL;
    return table(user_pt[index] & ADDRESS_MASK);
}
int vm_snapshot_begin(void) {
    if (snapshot || !user_pt) return 0;
    for (unsigned i = 0; i < USER_PAGES; i++) {
        saved_user[i] = user_pt[i];
        user_pt[i] &= ~PTE_WRITE;
        invalidate(USER_BASE + i * PAGE_SIZE);
    }
    snapshot = 1; return 1;
}
int vm_user_cow(uint64_t address) {
    if (!snapshot || address < USER_BASE || address >= USER_END) return 0;
    unsigned i = (address - USER_BASE) / PAGE_SIZE;
    if (!(saved_user[i] & PTE_WRITE) || (user_pt[i] & PTE_WRITE) ||
        (user_pt[i] & ADDRESS_MASK) != (saved_user[i] & ADDRESS_MASK)) return 0;
    uint64_t pa = pmm_alloc();
    if (!pa) return 0;
    bytes_copy(table(pa), table(saved_user[i] & ADDRESS_MASK), PAGE_SIZE);
    if (!pmm_set_mapped(pa, 1)) { (void)pmm_free(pa); return 0; }
    user_pt[i] = pa | PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_NX;
    invalidate(USER_BASE + i * PAGE_SIZE); return 1;
}
void vm_snapshot_end(int rollback) {
    if (!snapshot) return;
    for (unsigned i = 0; i < USER_PAGES; i++) {
        uint64_t old = saved_user[i] & ADDRESS_MASK, now = user_pt[i] & ADDRESS_MASK;
        if (old && old != now) {
            uint64_t discarded = rollback ? now : old;
            (void)pmm_set_mapped(discarded, 0); (void)pmm_free(discarded);
        }
        if (rollback) user_pt[i] = saved_user[i];
        else if (saved_user[i] & PTE_WRITE) user_pt[i] |= PTE_WRITE;
        invalidate(USER_BASE + i * PAGE_SIZE);
    }
    snapshot = 0;
}
void vm_user_clear(void) {
    vm_snapshot_end(1);
    if (!user_pt) return;
    for (unsigned i = 0; i < USER_PAGES; i++) {
        uint64_t pa = user_pt[i] & ADDRESS_MASK;
        user_pt[i] = 0; invalidate(USER_BASE + i * PAGE_SIZE);
        if (pa) { (void)pmm_set_mapped(pa, 0); (void)pmm_free(pa); }
    }
}
int vm_user_check(uint64_t address, size_t length, int write) {
    if (!length) return 1;
    if (!user_pt || address < USER_BASE || address >= USER_END || length > USER_END - address) return 0;
    unsigned first = (address - USER_BASE) / PAGE_SIZE;
    unsigned last = (address + length - 1 - USER_BASE) / PAGE_SIZE;
    for (unsigned i = first; i <= last; i++) {
        if (!(user_pt[i] & PTE_PRESENT)) return 0;
        if (write && !(user_pt[i] & PTE_WRITE) && !(snapshot && (saved_user[i] & PTE_WRITE))) return 0;
    }
    return 1;
}
int vm_user_prepare(uint64_t address, size_t length) {
    if (!vm_user_check(address, length, 1)) return 0;
    if (!length) return 1;
    unsigned first = (address - USER_BASE) / PAGE_SIZE;
    unsigned last = (address + length - 1 - USER_BASE) / PAGE_SIZE;
    for (unsigned i = first; i <= last; i++)
        if (!(user_pt[i] & PTE_WRITE) && !vm_user_cow(USER_BASE + i * PAGE_SIZE)) return 0;
    return 1;
}
static void user_copy(uint64_t address, void *data, size_t length, int out) {
    unsigned char *buffer = data;
    while (length) {
        unsigned index = (address - USER_BASE) / PAGE_SIZE;
        size_t offset = address & (PAGE_SIZE - 1), n = PAGE_SIZE - offset;
        if (n > length) n = length;
        unsigned char *physical = vm_user_physical(index);
        if (out) bytes_copy(physical + offset, buffer, n);
        else bytes_copy(buffer, physical + offset, n);
        address += n; buffer += n; length -= n;
    }
}
int vm_user_read(void *to, uint64_t address, size_t length) {
    if (!vm_user_check(address, length, 0)) return 0;
    user_copy(address, to, length, 0); return 1;
}
int vm_user_write(uint64_t address, const void *from, size_t length) {
    if (!vm_user_prepare(address, length)) return 0;
    user_copy(address, (void *)from, length, 1); return 1;
}
