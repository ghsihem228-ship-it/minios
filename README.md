# MiniOS

A small x86 operating system kernel written from scratch in C and assembly, built to learn how an OS really works.

## Goals

- [x] Boot via Multiboot (GRUB / QEMU)
- [x] VGA text-mode output
- [x] GDT and IDT, CPU exceptions ([notes](docs/02-gdt-idt.md))
- [x] Hardware interrupts (PIC, timer) ([notes](docs/03-interrupts-keyboard.md))
- [x] Keyboard driver
- [x] Physical memory manager (bitmap frame allocator) ([notes](docs/04-physical-memory.md))
- [x] Paging and a kernel heap (`kmalloc` / `kfree`) ([notes](docs/05-paging-heap-shell.md))
- [x] Interactive memory shell (`help`, `mem`, `mmap`, `alloc`, `free`, `heap`, `page`, `test`, `uptime`, `clear`)
- [x] Preemptive round-robin user processes, sleep/wakeup, kill and resource reaping
- [x] Ring 3, TSS kernel stacks and isolated per-process page directories
- [x] Checked `int 0x80` system calls and ELF32 program loading
- [x] RAM filesystem with file descriptors, read/write/seek and read-only built-ins
- [x] Filesystem and process commands (`ls`, `cat`, `write`, `rm`, `run`, `spawn`, `ps`, `kill`)

See [the v0.6 design and syscall ABI](docs/06-processes-userspace-ramfs.md).

## Requirements

- `gcc` with 32-bit support (`gcc-multilib`)
- `nasm`
- `ld` (binutils)
- `qemu-system-i386`
- Python 3 (for the automated QEMU tests only)

On Debian/Ubuntu:

```bash
sudo apt install build-essential gcc-multilib nasm qemu-system-x86 python3
```

## Build and run

```bash
make        # builds build/minios.bin
make run    # boots the kernel in QEMU
make test   # headless functional tests (Python 3 + QEMU TCG, no GUI or KVM)
make clean
```

Expected output: MiniOS v0.6, a live uptime counter, green `[ OK ]` lines
including memory, RAMFS and ELF loader self-tests, then `minios>`.
Try `help`, `mem`, `alloc 9001`, `heap`, `free 0`, then `test`.
Numbers accept decimal and `0x` hexadecimal; allocations use 16 numbered slots.
`fault` deliberately reads the unmapped NULL page and halts with a diagnostic;
restart QEMU afterwards. A fault in a Ring 3 program instead terminates only
that process. Try these commands inside the MiniOS prompt:

```text
ls
run /bin/hello
run /bin/probe
run /bin/filedemo
cat /tmp/user.txt
run /bin/sleep
run /bin/crash
ps
spawn /bin/spin
ps
```

Use `kill <pid>` with the PID printed by `spawn` to stop the CPU-bound task.
`run` waits for its program; press Esc to cancel a foreground program.
`spawn` returns immediately. `write /tmp/note hello`, `cat /tmp/note` and
`rm /tmp/note` exercise mutable files. RAMFS contents are lost on reboot.

The heap grows to about 4 MiB, retains its backing pages after `kfree`, and
coalesces adjacent free blocks. `trim` returns fully free trailing pages to
the PMM. Boot stress tests trim back down afterwards, preserving RAM for user
programs. This remains an educational single-CPU kernel with a non-preemptible
kernel core: user tasks are timer-preempted, but kernel paths run to completion.
There is no persistent disk filesystem, networking, SMP, dynamic linker,
fork, demand paging or floating-point context support yet.

For cloud shells with the prepared local tools:

```bash
export PATH=/workspace/.minios-tools/bin:$PATH
make test
```

`make run` needs a graphical display. `make test` inspects VGA memory via QMP,
types commands through the PS/2 keyboard, and checks actual guest behavior.
It boots with 2, 64 and 128 MiB, and ends each guest with a deliberate page
fault to verify CR2/error reporting. A PASS line is required for each RAM size.
Tests also execute user ELF programs, exercise filesystem syscalls, verify
memory/privilege faults are confined to their process and check for frame
leaks. The 64 MiB run checks two CPU-bound tasks progress without yielding,
private address spaces at identical virtual addresses, task limits and reuse.
The suite types many commands and can take a few minutes; it runs without a
QEMU window until `make test` completes.
To select another QEMU binary or RAM sizes:

```bash
python3 tests/smoke.py --qemu qemu-system-i386 --ram 2 64 128
```

## Project layout

```
boot/boot.asm         Multiboot header and entry point
boot/gdt_flush.asm    Loads the GDT and reloads segment registers
boot/isr_stubs.asm    Entry stubs for CPU exceptions (0-31) and IRQs (32-47)
boot/programs_blob.asm Embeds the built user ELF binaries
kernel/kernel.c       kernel_main
kernel/vga.c          VGA text-mode driver (status bar, cursor)
kernel/gdt.c          Global Descriptor Table
kernel/idt.c          Interrupt Descriptor Table
kernel/isr.c          CPU exception handler
kernel/pic.c          8259 PIC remap, mask and end-of-interrupt
kernel/irq.c          Hardware interrupt dispatch
kernel/timer.c        PIT timer (100 Hz) and uptime
kernel/keyboard.c     PS/2 keyboard driver
kernel/pmm.c          Physical memory manager (bitmap frame allocator)
kernel/paging.c       Page directory, identity map, heap mappings, TLB invalidation
kernel/heap.c         Growable first-fit allocator, split/coalesce, integrity tests
kernel/shell.c        Bounded input, memory commands and allocation slots
kernel/task.c         Process table, preemption, sleep/wakeup, exit/kill/reaping
kernel/elf.c          Validated ELF32 loader and rollback tests
kernel/syscall.c      Ring 3 ABI, user pointer checks and descriptor operations
kernel/ramfs.c        Bounded RAM filesystem (read-only built-ins + mutable files)
kernel/programs.c     Installs embedded /bin programs into RAMFS
user/                Freestanding NASM programs, syscall macros and linker script
tests/smoke.py        Headless QEMU functional tests
docs/                 Design notes, one per milestone
linker.ld             Linker script (kernel loaded at 1 MiB)
Makefile
```

## Roadmap notes

Each milestone should land as its own commit (or small PR) with a short note in `docs/` explaining the design: why the GDT looks the way it does, how the frame allocator tracks memory, how a context switch saves registers, and so on. Those notes are what make the repo valuable to readers.

## References

- OSDev Wiki: https://wiki.osdev.org
- Intel 64 and IA-32 Architectures Software Developer's Manuals
- Operating Systems: Three Easy Pieces (free online)

## License

MIT
