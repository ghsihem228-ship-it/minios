# Milestone 2: GDT, IDT and CPU exceptions

## Why this milestone

Right after boot, the CPU runs with whatever descriptor tables the bootloader left behind. A kernel must own them. Without an IDT, any CPU fault (a divide by zero, a bad memory access) becomes a triple fault and the machine silently resets, which makes debugging nearly impossible. With an IDT we get a readable error message instead.

## The GDT (`kernel/gdt.c`, `boot/gdt_flush.asm`)

Protected mode still requires segment descriptors. MiniOS uses the flat model: every segment covers the full 4 GiB, so segmentation effectively does nothing and paging (a later milestone) does the real work.

| Index | Selector | Purpose |
|-------|----------|---------|
| 0 | 0x00 | Null descriptor (required) |
| 1 | 0x08 | Kernel code, ring 0 (access `0x9A`, flags `0xCF`) |
| 2 | 0x10 | Kernel data, ring 0 (access `0x92`, flags `0xCF`) |

- Access byte `0x9A` = present, ring 0, code segment, executable, readable.
- Access byte `0x92` = present, ring 0, data segment, writable.
- Flags `0xCF` = 4 KiB granularity, 32-bit segment, plus limit bits 16-19 (`0xF`).

`gdt_flush` runs `lgdt`, reloads `ds`, `es`, `fs`, `gs`, `ss` with selector `0x10`, then does a far jump to reload `cs` with selector `0x08`. A far jump is the only way to change `cs`.

## The IDT (`kernel/idt.c`)

256 entries, 8 bytes each. Entries 0-31 are reserved for CPU exceptions. Each entry holds the handler address, the code selector (`0x08`) and a flags byte. `0x8E` means: present, ring 0, 32-bit interrupt gate.

## Exception entry (`boot/isr_stubs.asm`)

Some exceptions push an error code (8, 10-14, 17, 21, 29, 30) and the rest do not. To give the C handler one uniform layout, stubs for exceptions without an error code push a dummy `0`. Every stub then pushes its interrupt number and jumps to `isr_common`, which:

1. saves all general registers with `pusha`,
2. saves `ds` and loads the kernel data segment,
3. clears the direction flag (the C ABI requires it),
4. calls `isr_handler(struct regs *)` with a pointer to the saved state,
5. restores everything and executes `iret`.

The field order of `struct regs` in `kernel/isr.h` mirrors exactly what the assembly pushes. A `_Static_assert` checks its size at compile time.

## Testing

`kernel_main` raises a breakpoint exception (`int $3`) on purpose. The handler prints a message and returns, and the next line of output proves execution resumed correctly.

To test a fatal exception, uncomment the divide-by-zero lines in `kernel/kernel.c`. The kernel prints `Divide by zero` with the faulting `eip` and halts.

## Things to try

- Trigger other exceptions: an invalid opcode with `__asm__ volatile ("ud2")`, or a general protection fault by loading a bad segment selector.
- Use `objdump -d build/minios.bin` and compare the faulting `eip` shown on screen with the disassembly.

## Next

Hardware interrupts: remap the PIC, program the timer (IRQ0) and add a keyboard driver (IRQ1).
