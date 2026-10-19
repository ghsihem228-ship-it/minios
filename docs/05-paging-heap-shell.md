# Paging, kernel heap and memory shell (v0.5)

This extends v0.4's Multiboot memory map and physical frame allocator. The
kernel now enables paging, allocates variable-sized objects and exposes an
interactive shell. The PMM still owns physical frame accounting.

## Address layout

| Virtual range | Purpose |
| --- | --- |
| `0x00000000`–`0x00000FFF` | Unmapped: catches NULL dereferences |
| `0x00001000`–`0x003FFFFF` | Supervisor RW identity map: kernel, stack, tables, VGA, boot information |
| `0xD0000000`–`0xD03FDFFF` | Heap, backed on growth by separately allocated 4 KiB frames |
| `0xD03FE000`–`0xD03FFFFF` | Temporary self-test mappings; unmapped when idle |
| Other addresses | Unmapped |

The page directory and two page tables are statically allocated and 4 KiB
aligned inside the kernel image. PMM reserves that image, including the
tables. Initialization refuses a kernel that would exceed the low identity
map. CR3 points at the directory; CR0.PG and CR0.WP are enabled. No user bit
is set. `paging_map_page` and `paging_unmap_page` mutate only the heap's
4 MiB window, reject misaligned/existing/absent mappings as appropriate,
and invalidate the affected TLB entry with `invlpg`. They do not own frames.
`paging_translate` reports an existing physical address including its offset.

This bootstrap is intended for the documented direct QEMU Multiboot boot.
Boot information and its memory map must be in the low identity-mapped
range; GRUB modules are not supported. Low mappings intentionally include
holes/firmware and remain writable; this is not a per-section hardened or
user-space address layout. Changing a page mapping is an internal kernel
API, not a shell operation.

## Heap

`kmalloc` returns a 16-byte-aligned payload, or NULL for zero, oversized
requests or exhaustion. Each block has a 16-byte header with size, state
magic and neighboring block pointers. First-fit allocation splits only if
there is room for another header plus an aligned payload. `kfree` searches
the list by exact payload address before touching metadata, ignores NULL,
interior/unknown pointers and duplicate frees, and merges adjacent free
blocks in both directions. Like a conventional allocator, it cannot detect
a stale pointer once its address has been reallocated to another caller.

The heap initially maps one page. Growth rounds up to pages, obtains PMM
frames and maps them before changing the block list. Partial failure unmaps
and returns every frame from that attempt. Growth extends the final free
block when possible. The ceiling is 4,186,112 bytes including headers;
the maximum single payload is 4,186,096 bytes. Free pages remain mapped for
reuse, so `kfree` reduces heap usage but does not increase PMM free frames.
Allocated memory is uninitialized. Only the main kernel context uses the
allocator; interrupt handlers must not call it. There is no allocator lock,
heap shrinking, guard per allocation or recovery from overwritten headers.

`heap_validate` checks exact coverage, size/alignment, linkage, state magic
and the absence of neighboring free blocks. Statistics separate committed
bytes, allocated block capacity, free payload, largest free block and block
count. Used/free figures exclude headers and include alignment padding.

## Shell

The prompt accepts up to 127 characters. Longer lines are discarded as a
whole. Backspace cannot erase the prompt; tabs become spaces. At most two
tokens are accepted by commands, with checked decimal/hexadecimal conversion.

| Command | Behavior |
| --- | --- |
| `help`, `clear`, `uptime` | List commands, clear output, show ticks at 100 Hz |
| `mem`, `mmap`, `heap` | PMM totals and paging state, boot memory map, heap statistics/integrity |
| `alloc <bytes>` | Allocate into the first available slot (0–15), print virtual address |
| `free <id>` | Free an occupied slot; no raw address input |
| `page <addr>` | Show translation or report unmapped |
| `test` | Repeat paging/heap tests; free shell allocations first |
| `fault` | Deliberately read address zero and halt |

The page-fault handler reports CR2, EIP, raw error code and decoded
presence/access/privilege bits. Fatal exceptions leave the screen visible
with interrupts disabled. No automatic recovery is attempted.

## Validation

Boot runs the existing PMM test, a paging test and a heap test. The paging
test checks NULL is absent, independent frames at high virtual addresses,
end-of-page writes, offset translation, duplicate-map rejection, unmapping,
remapping and alias visibility, and restores the PMM free-frame count.
The heap test checks splitting/coalescing/reuse, alignment, multi-page data
patterns, invalid/duplicate frees, zero/overflow requests, repeated 64 KiB
allocations until exhaustion and recovery to one free block. PMM consumption
must exactly match retained heap growth, including after failed growth.

`make test` builds and runs `tests/smoke.py` against the actual binary at
2, 64 and 128 MiB using TCG. The 2 MiB run exercises physical exhaustion and
rollback; the 64 MiB run also checks all allocation slots, fragmentation,
full heap capacity, command parsing, overlong input and editing. All runs
check memory commands, repeat self-tests, keyboard input, advancing timer
ticks and a real fatal NULL page fault. Tests use Python's standard library
and QMP's physical-memory dump; no guest serial output is required. Each
guest is terminated after its checks, even on failure. A missing expected
message or a guest exception before the deliberate fault fails the test.

## Next

Multitasking needs per-task stacks, saved CPU contexts and allocator
synchronization. User mode additionally needs per-process page directories,
user/supervisor permissions and system-call entry points. Filesystem and
process commands should follow those components rather than fake their output.
