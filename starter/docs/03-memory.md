> Historical step-3 design, preserved from its original README. See ../README.md
> and abi-v1.md for the current implementation and later extensions.

# Step 3: physical and virtual memory management

The independent **C11/x86_64** kernel now consumes the Multiboot memory map,
allocates physical frames, and replaces the temporary 1-GiB huge-page identity
mapping with protected 4-KiB page tables. This directory is the current kernel;
the repository root continues to build the older MiniOS i386 project.
[Boot](01-boot.md) and [interrupts](02-interrupts.md) notes describe the
previous milestones, not the current output or memory protections.

## Alternatives and recommendation

| Part | Alternative A | Alternative B | Recommendation now |
| --- | --- | --- | --- |
| Physical allocation | Bitmap: one bit per frame, very compact, needs separate ownership metadata | Buddy lists: efficient power-of-two contiguous allocations, more split/merge bookkeeping | A bounded frame-state array with a rotating scan: simple ownership checks before adding buddy/contiguous allocation |
| Paging | Keep 2-MiB huge pages: lower table cost and fewer TLB entries | 4-KiB pages: more tables, precise text/data/stack protections | 4-KiB pages for this first manager; huge pages can be added for suitable RAM regions later |
| Kernel addressing | Low identity/direct mappings: easy transition from bootstrap | Move the whole kernel into the higher half: better separation of address-space roles but requires relocation/boot changes | Retain low kernel addresses, add a small high canonical allocation arena; defer kernel relocation |
| Object allocation | Page allocator: simple zeroing and ownership, wasteful for small objects | Slab/size-class heap: efficient small allocations, additional lifetime and fragmentation rules | Working page allocation now; add slab/`kmalloc` when kernel objects need it |

The chosen frame-state array costs **256 KiB** for a fixed **first-1-GiB physical
window**. It is not a claim that all installed RAM is managed. Physical memory
above the ceiling is ignored; boot metadata must fit the temporary window.
A bitmap plus ownership maps could reduce this cost; the byte states make
invalid frees and mapping ownership straightforward to verify first. No dynamic
heap or scheduler is needed to initialize it.

## Specification: boot memory and physical frames

`kernel_main(uint32_t boot_info)` receives the saved Multiboot EBX address in
EDI. Assembly preserves it before CPUID and checks NX support before enabling
EFER.NXE/LME. A long-mode CPU without NX fails with a serial error before entry
to 64-bit C. NX is required rather than silently dropping execution protection.

The decoded structures and internal allocator API are:

```c
struct memory_range { uint64_t base, length; uint32_t type; };
struct boot_memory {
    struct memory_range ranges[128];
    unsigned count;
};
struct phys_range { uint64_t base, length; };

enum frame_state {
    FRAME_RESERVED, FRAME_FREE, FRAME_OWNED, FRAME_TABLE, FRAME_MAPPED
};

int bootmem_parse(const void *bytes, size_t length, struct boot_memory *out);
int pmm_init(const struct boot_memory *map,
             const struct phys_range *reserved, unsigned count);
uint64_t pmm_alloc(void);       /* physical address; 0 on exhaustion */
int pmm_free(uint64_t physical);
uint64_t pmm_free_count(void);
```

These are kernel-only functions; there are still **no user syscall signatures
or stable application ABI** in this starter.

The complete implementation is in `bootmem.c`, `bootinfo.c` and `pmm.c`:

1. Require Multiboot's mmap flag. Decode little-endian, variable-size records
   without alignment assumptions. The payload size must be at least 20 bytes;
   extensions are skipped. Reject truncation, length/addition overflow, an
   empty effective map, more than 128 nonempty ranges or more than 64 KiB of
   map input. A rejected map exposes zero decoded records. Do not fall back
   to `mem_upper` and guess at reserved regions.
2. Unknown/non-type-1 regions remain reserved. Make only **fully contained**
   4-KiB usable frames free; a reserved range blocks **every touched frame**.
   Process usable ranges first and reserved ranges second, so unsorted or
   overlapping available entries cannot override firmware reservations.
3. Permanently exclude the first 1 MiB, the entire linked image/BSS including
   all stacks, the boot-info prefix being read, mmap storage, module table
   and each module payload. Module counts are capped at 32; descriptors are
   bounded and start/end checked. Modules above the managed ceiling are
   outside the allocator anyway. This stage reserves but does not execute
   modules; unused boot strings and symbols are not retained or dereferenced.
4. Metadata pointers are range-checked against the temporary identity window
   before reads. This bounds arithmetic/accesses; it does not authenticate
   the loader or prove a malicious firmware address denotes RAM. The loader
   and firmware are trusted inputs. The pure byte decoder handles malformed
   map content without depending on that physical-address trust.
5. Allocation changes FREE to OWNED and returns a unique aligned physical
   address. Address zero is never allocatable. A rotating hint avoids scanning
   from frame zero on every allocation; worst-case scan is linear in the
   bounded frame count. Failure changes no ownership/count.
6. Free accepts only OWNED. Reject unaligned, reserved, already freed, mapped,
   out-of-range and page-table frames. Tables use a separate permanent TABLE
   state. A frame used by the arena changes OWNED -> MAPPED; unmapping changes
   it back before the caller may free it. The manager does not yet record
   process identities or arbitrary reference counts.

## Specification: page tables and page allocation

```c
#define VM_ARENA UINT64_C(0xffff800000000000)
#define VM_ARENA_PAGES 512

int memory_init(uint32_t boot_info); /* once, IF=0; failure is fatal to boot */
int vm_map_page(uint64_t virtual_address, uint64_t physical);
int vm_unmap_page(uint64_t virtual_address, uint64_t *physical);
int vm_alloc_page(uint64_t virtual_address);
int vm_free_page(uint64_t virtual_address);
int vm_lookup(uint64_t virtual_address, uint64_t *physical, uint64_t *flags);
```

All mutating calls run on the **single bootstrap CPU, with IF=0**, outside ISR
context. That is a caller invariant, not an SMP lock. Raw `pmm_alloc` contents
are unspecified; `vm_alloc_page` zeroes all 4096 bytes before publishing a map.
The caller supplies a page-aligned VA in the fixed, canonical 2-MiB arena.
There is no virtual-address search, multi-page contiguous allocator or demand
paging yet. Invalid/occupied addresses fail without allocating a frame.

Page-table invariants:

- Build a fresh four-level tree in owned TABLE frames while the bootstrap
  identity window is still active. Zero each new table before linking it.
  Set only supervisor upper entries; no huge-page entries are installed.
- Map manageable free/allocated RAM at matching low addresses as RW/NX. Omit
  firmware holes and reserved loader/module ranges. Map the linked kernel
  explicitly: text **RX**, rodata **R/NX**, data/BSS/stacks **RW/NX**. Section
  boundaries are page-aligned, so text and writable data cannot share a page.
- The physical text/rodata frames are reserved, so they receive no writable
  arena alias. Arena mappings accept only OWNED frames and enforce one such
  alias per frame. There is still an internal RW/NX identity alias for owned
  ordinary RAM. This is trusted kernel access, not a user-facing capability.
- Leave virtual page zero and a dedicated 4-KiB page immediately below each
  bootstrap/#DF/NMI/#MC stack **not present**. These physical guard pages are
  also reserved; the public arena API cannot map them back in.
- Switch CR3 only after the new tables map the current code, stack, IDT/GDT/TSS
  and tables. Set CR0.WP so ring-0 stores honor read-only leaves. EFER.NXE was
  already checked and enabled; there is no executable writable leaf.
- Preallocate the arena's upper tables and its single PT during boot. Runtime
  mapping therefore needs no fallible intermediate-table allocation. Reject
  duplicate aliases and occupied entries without changing ownership. Unmap
  clears the PTE and executes INVLPG **before** returning frame ownership.
  Remapping the same VA to a different physical frame invalidates the old TLB
  entry. No page table is freed while reachable.
- Boot allocation/table-build failure halts explicitly; there is no recovery
  attempt or retry using partially built tables. These permanent tables last
  for the entire current address space. Runtime page-allocation failure frees
  its newly acquired frame if mapping fails.

Working example, **only inside a kernel critical section with IF=0**:

```c
if (vm_alloc_page(VM_ARENA)) {
    volatile uint64_t *buffer = (volatile uint64_t *)(uintptr_t)VM_ARENA;
    buffer[0] = UINT64_C(0x123456789abcdef0);
    /* buffer is writable, supervisor-only and non-executable. */
    vm_free_page(VM_ARENA);
    /* buffer is now unmapped and must never be dereferenced. */
}
```

This is page allocation rather than `kmalloc`/`kfree` for arbitrary byte sizes.
The source has comments at the ownership, CR3/WP and TLB boundaries.

## Build, project structure and output

The existing packages remain sufficient on Ubuntu/WSL:

```bash
sudo apt install build-essential binutils nasm qemu-system-x86 python3
cd ~/minios
git pull --ff-only origin main
cd starter
make
make test
make run
```

```text
starter/
  boot/entry.asm         preserve boot info, CPU/NX checks, long-mode entry
  boot/interrupts.asm    existing interrupt stubs plus broken-stack test entry
  linker.ld             page-separated sections, four guarded static stacks
  kernel/bootmem.c/.h    bounded, host-testable Multiboot mmap decoder
  kernel/bootinfo.c      bounded loader-data capture and module reservations
  kernel/pmm.c/.h        physical frame states, ownership, allocate/free
  kernel/vmm.c/.h        protected page tables, mapping and zeroed page APIs
  kernel/memory_test.c   deliberately fatal memory test operations
  kernel/interrupts.c    existing IDT/TSS/PIC/PIT; page-aligned IST stacks
  tests/test_boot.py    real CPU faults and independent live-table inspection
  tests/test_memory.c   sanitizer unit/model/fuzz tests of decoder and PMM
```

Expected normal Serial output:

```text
Hello
IDT ready: 256 gates; TSS/IST ready
PASS INT3/IRQ0: 64-bit GPRs, DF/IF, IRETQ; spurious IRQ7/15
PASS PMM: memory map, ownership, exhaustion/reuse
PASS VMM: 4 KiB, W^X, NULL/stack guards, map/unmap
PASS PIT: IRQ0 ticking at ~100 Hz
READY: interrupts enabled; idle
```

Silence after READY is expected; timer interrupts continue. Ctrl+C exits QEMU.
There is no guest command interpreter yet. Normal `make run` still attaches
no network/disk/GUI. Boot `build/kernel.bin`, keep `build/kernel.elf` for symbols.
`make clean` removes only this starter's build outputs. GDB remains optional
via `make debug`, and is not required by the validation suite.

## Validation: what is actually tested

`make test` runs **21 QEMU TCG scenarios** and the host sanitizer tests.
`make test-host` runs only the host tests. Deadlines bound every VM and tests
terminate only their own processes; test module payloads live in temporary files.

1. Normal boots at **2/16/64/128/512 MiB** retain the interrupt/register/IST/PIT
   checks. Boot self-tests verify distinct frames, reserved/table/double-free
   rejection, mapped-frame ownership, failed aliases, arena edge addresses,
   offsets/noncanonical lookup and TLB behavior when reusing a VA.
2. Exhaust the actual PMM, require allocation failure without count corruption,
   and free its boot-only owners back to the baseline. With memory exhausted,
   contaminate one owned frame, free exactly that one, then allocate a virtual
   page. It must use the same physical frame and erase **every word**. OOM in
   virtual allocation must also return failure. One zeroed inspection/demo
   arena page remains allocated after the boot self-test.
3. QMP reads the **actual CR3 and entire live table tree**, not just constants.
   Independently verify every reachable table's ownership, table counts,
   supervisor bits, absence of huge pages, identity/arena address routing,
   W^X, RX text, R/NX rodata, RW/NX data/tables, null/stack guards, CR0.WP and
   EFER.NXE. No executable alias may lie outside linked text.
4. The earlier genuine #UD/#GP/#PF/#DF and injected NMI cases still pass.
   Additional images deliberately trigger a NULL read (PF error 0), text
   write (PF error 3), arena execution (PF error `0x11`), guard read (PF error
   0), and stale-pointer read after unmap/free (PF error 0). Check CR2 exactly.
5. A broken-stack test sets RSP at the stack bottom then executes UD2. Delivery
   cannot write the guarded stack; double fault must reach the separate IST
   and print its verified frame. This tests actual protection rather than
   just the artificial non-present #GP gate used in step 2.
6. Missing/corrupt mmap inputs must print a boot error and halt before PMM
   success. A CPU without NX must fail early; unsupported long mode remains
   tested. A real Multiboot `-initrd` module with a partial final frame must
   remain reserved and byte-for-byte intact through the allocator tests.
7. Host ASan/UBSan checks record extensions, truncation, overflow, count limits,
   reserved overlaps, partial edges and clipping above 1 GiB. Run **100000**
   bounded decoder fuzz cases, mixing random bytes with valid/mutated maps.
   Compare **50000** allocator ownership operations against an independent
   shadow model, then check exhaustion, mapped/table free rejection and reuse.

QEMU fault images must report exactly once, disable IF, freeze timer ticks and
remain in the deliberate halt loop, rather than reset or triple-fault the VM.
Host tests exercise the same decoder/allocator sources linked into the kernel.
The page-table manipulator needs architectural CPU tests rather than a mock
that merely repeats its bit operations. GDB itself and real hardware were not
tested; neither are actual machine-check delivery/recovery or SMP TLB shootdowns.

## Risks, limits and common mistakes

- Bounds/overflow/rounding errors can put firmware, kernel or module memory
  on the free list. Reserved overlaps must win, including partially covered
  edge frames. Copy needed loader data before reusing its storage. Metadata
  parsing cannot protect against deliberately dishonest firmware.
- Page protections require **both** correct PTEs and CR0.WP/EFER.NXE. A writable
  alias to text defeats read-only text. A broad identity map or guard alias
  defeats a not-present guard. Keep virtual addresses distinct from physical
  addresses even where the early direct map currently makes them equal.
- Never free mapped or reachable table frames, reuse a frame before TLB
  invalidation, or switch CR3 before mapping the currently executing stack
  and interrupt state. These bugs can look like unrelated allocator corruption.
- One 4-KiB guard catches adjacent downward stack overflow; a large stack jump
  can skip it. There are no compiler stack probes, automatic stack growth or
  nested IST services. The early bootstrap still uses broad RWX mappings
  until the protected root is activated; this is not end-to-end boot hardening.
- All kernel mappings are supervisor-only, but all current code is ring 0.
  There is no user address space, process isolation, capability authority,
  KASLR, SMP synchronization, shootdown protocol, swapping, COW or demand-page
  recovery. Table memory is writable to trusted kernel code by design.
- The physical ceiling is **1 GiB**, the virtual allocation arena **512 pages**,
  and tables consume roughly 4 KiB per mapped 2 MiB plus upper levels. There
  is no generic heap, contiguous DMA allocator or table reclamation API. Raw
  physical allocation is not zero-filled; use the zeroed page API where data
  exposure matters. These limits are explicit, not production scalability.
- Fatal protection faults still stop the entire current kernel. They do not
  recover faulty code or kill a user process; user-mode fault policy comes
  after scheduling and process address spaces.

Step 3 ends here. Wait for explicit **next** before step 4 (scheduler). Keep the
later object/handle, syscall, capability and other features in their requested
order rather than implementing them automatically.

## Subsequent implementation

Frame metadata now uses four bits per frame (128 KiB for the 1 GiB ceiling), preserving the existing ownership states and 2 MiB boot support.
