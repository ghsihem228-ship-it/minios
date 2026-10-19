# Milestone 4: physical memory manager

## Why this milestone

Paging, the kernel heap and task stacks all need the same thing: a way to ask "give me a free 4 KiB chunk of RAM". The physical memory manager (PMM) answers that. It does not create virtual addresses; it only tracks which physical frames are in use.

## Where the memory map comes from

`boot.asm` sets the `MEMINFO` flag in the Multiboot header, so the bootloader fills in a memory map and passes a pointer to it in `ebx` (the `mb_info` argument of `kernel_main`). Each entry has a base address, a length and a type; type 1 is usable RAM. Entries are variable size: the next one starts at `entry + entry->size + 4`.

Example from QEMU with 64 MiB:

```
0x00000000 - 0x0009FBFF  usable      (low memory)
0x0009FC00 - 0x000FFFFF  reserved    (BIOS, VGA, ROMs)
0x00100000 - 0x03FDFFFF  usable      (the RAM we really use)
0x03FE0000 - 0x03FFFFFF  reserved
0xFFFC0000 - 0xFFFFFFFF  reserved    (firmware)
```

## The bitmap

One bit per 4 KiB frame: `1` = used or reserved, `0` = free. Covering the full 32-bit space takes 1,048,576 bits = 128 KiB, kept as a static array in `.bss`, so it needs no allocator to bootstrap itself.

`pmm_init` works in two steps:

1. Mark everything used, then clear the bits for every frame fully inside a usable region (base rounded up, end rounded down to a frame boundary).
2. Re-reserve what must never be handed out: the first 1 MiB, the kernel image (`kernel_start` to `kernel_end`, symbols from `linker.ld`, which also covers the bitmap), and the Multiboot info and memory map themselves.

Starting from "all used" is the safe default: any address the firmware did not explicitly declare as RAM can never be allocated.

## Allocation

`pmm_alloc_frame` scans the bitmap one 32-bit word at a time, skipping words that are `0xFFFFFFFF`, and uses a next-fit hint (the word where the last search ended) so repeated allocations do not rescan the start of memory. It returns the physical address, or `0` on failure. Frame 0 is always reserved, so `0` can never be a valid result.

`pmm_free_frame` clears the bit and moves the hint back if needed. It ignores misaligned addresses, already-free frames, the low 1 MiB and the kernel image.

Limitation: the bitmap cannot distinguish a hole in the memory map from an allocated frame, so freeing an address that was never allocated is a caller bug.

## Testing

`kernel_main` prints the memory map and totals, then runs a self-test: allocate two frames, free one, allocate again and check the same frame comes back and the free count is consistent. On QEMU with `-m 64` it reports 63 MiB usable.

Try `make run` with different `-m` values and watch the totals change.

## Things to try

- Add `pmm_alloc_frames(n)` for contiguous runs (needed for DMA buffers).
- Zero a frame after allocating it and measure the cost.
- Write a stress test that allocates until exhaustion, then frees everything and checks the free count matches the start.

## Next

Paging: build a page directory, identity-map the kernel, enable `CR0.PG`, then build a kernel heap (`kmalloc` / `kfree`) on top of the frame allocator.
