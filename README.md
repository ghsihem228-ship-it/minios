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
- [x] VFS mount routing, primary-master ATA PIO and persistent FAT32 files at `/disk`
- [x] Disk-backed ELF execution, multi-cluster writes, sparse gaps and flush/error handling
- [x] PCI discovery, RTL8139 Ethernet DMA and bounded packet polling
- [x] IPv4, ARP, DHCP, ICMP echo, DNS lookup/cache and TFTP downloads to disk

See [the v0.6 design and syscall ABI](docs/06-processes-userspace-ramfs.md).
Persistent storage and its limits are documented in [v0.7 storage notes](docs/07-vfs-ata-fat32.md).
Networking and its limits are documented in [v0.8 networking notes](docs/08-networking.md).

## Requirements

- `gcc` with 32-bit support (`gcc-multilib`)
- `nasm`
- `ld` (binutils)
- `qemu-system-i386`
- Python 3 (for disk/network example preparation and automated QEMU tests)
- AddressSanitizer/UndefinedBehaviorSanitizer runtime libraries (included by Ubuntu/Debian GCC)

On Debian/Ubuntu:

```bash
sudo apt install build-essential gcc-multilib nasm qemu-system-x86 python3
```

## Build and run

```bash
make        # builds build/minios.bin
make run    # boots the kernel in QEMU
make test   # headless functional tests (Python 3 + QEMU TCG, no GUI or KVM)
make test-storage # only persistence/failure tests
make test-network # network integration and sanitized packet decoder tests
make clean
```

Expected output: MiniOS v0.8, a live uptime counter, green `[ OK ]` lines
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

## Persistent disk (v0.7)

`make run` creates `disks/minios.img` as a sparse 64 MiB FAT32 image on first
use, then attaches it as QEMU's primary IDE disk. Existing images are preserved;
`make clean` removes build outputs and **keeps the disk**. The image is ignored
by Git. `make disk` can create it without starting QEMU. A different dedicated
image can be selected with `make run DISK_IMAGE=disks/other.img`.

Inside MiniOS, try:

```text
df
write /disk/note.txt hello
append /disk/note.txt world
cat /disk/note.txt
cp /bin/hello /disk/hello.elf
run /disk/hello.elf
run /bin/diskdemo
ls /disk
sync
```

Close QEMU, run `make run` again and `cat /disk/note.txt`: `helloworld` survives.
`/tmp` remains volatile RAMFS; `/disk` is persistent. FAT names are case-insensitive
8.3 names (for example `NOTE.TXT`); long filenames and disk subdirectories are
not implemented. `cp` creates a new destination and refuses to overwrite one.
`stat <path>` reports size/access, and `rm <path>` deletes a closed file.
`/bin/diskdemo` tests Ring 3 file I/O, a sector-boundary overwrite, zero-filled
seek gaps and `stat`/`unlink`/`sync`; it leaves `/disk/USER.DAT` for inspection.

The kernel never formats a disk. Unsupported/corrupt volumes stay unmounted;
unclean volumes mount read-only. I/O errors are reported and disable further
writes. Successful file writes flush data and metadata, but FAT32 has no journal
here: interrupted operations may need offline filesystem repair. This disk image
holds files, not a standalone bootloader; QEMU still boots via `-kernel`.

The heap grows to about 4 MiB, retains its backing pages after `kfree`, and
coalesces adjacent free blocks. `trim` returns fully free trailing pages to
the PMM. Boot stress tests trim back down afterwards, preserving RAM for user
programs. This remains an educational single-CPU kernel with a non-preemptible
kernel core: user tasks are timer-preempted, but kernel paths run to completion.
There is no networking, SMP, dynamic linker, fork, demand paging or
floating-point context support yet. Storage uses polling, not DMA/AHCI/NVMe.

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
Storage tests then use fresh temporary images and QEMU processes to verify
reboot persistence, independent on-disk chain/mirror/ownership checks, FAT32
directory growth and reuse, MBR offsets, ENOSPC rollback and injected ATA
write/flush failures. They never open `disks/minios.img`. If `fsck.fat` is
installed, its read-only check must also pass; it is optional (`dosfstools`).
To select another QEMU binary or RAM sizes:

```bash
python3 tests/smoke.py --qemu qemu-system-i386 --ram 2 64 128
```

## Networking (v0.8)

`make run` also attaches an RTL8139 NIC to QEMU's user network. It prepares
`netfiles/welcome.txt` and `netfiles/hello.elf` for QEMU's built-in TFTP server,
preserving existing files. No root, TAP interface, external server or Internet
connection is required for this demonstration:

```text
pci
dhcp
net
ping 10.0.2.2
arp
fetch 10.0.2.2 welcome.txt /disk/net.txt
cat /disk/net.txt
fetch 10.0.2.2 hello.elf /disk/net.elf
run /disk/net.elf
```

The default QEMU network gives the guest `10.0.2.15`, gateway/TFTP server
`10.0.2.2` and DNS proxy `10.0.2.3`. `resolve example.com` uses that DNS proxy
and requires the host to have working DNS access. Tests use a local DNS fixture
instead of public servers. `fetch` takes a numeric IPv4 server address and a
relative remote filename, downloads through TFTP, then creates a new local file;
it refuses existing destinations, including FAT case aliases. Press Esc to
cancel network waits. Downloads are limited to 256 KiB and 30 seconds. RAMFS
retains its 4 KiB per-file limit; use `/disk` for larger files.

Put your own files under `netfiles/`, or select a separate directory with
`make run NET_FILES=/path/to/files`. QEMU exposes this host directory through
its TFTP service to the guest. `make clean` preserves it. With an unsupported or
absent NIC the kernel stays usable offline. This milestone has no TCP, HTTP,
TLS, IPv6, user socket API, or automatic DHCP renewal. See the design note for
protocol limits and the meaning of `net` counters.

`make test-network` boots actual RTL8139 hardware emulation at 2/64 MiB and tests
DHCP, ping, TFTP and downloaded Ring 3 ELF execution. An independent local
Ethernet peer exercises DNS compression/CNAME/cache/errors, invalid packets,
packet loss, retransmission, transfer IDs, size limits, background scheduling,
cancellation and resource recovery. Independent FAT inspection and a fresh VM
check downloaded bytes and persistence. A host sanitizer harness also runs
100,000 malformed-packet cases against the kernel's DNS/DHCP decoders.

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
kernel/vfs.c          Common file operations and RAMFS/FAT32 mount routing
kernel/block.c        ATA PIO sector read/write/flush with bounded polling
kernel/fat32.c        FAT32 root files, allocation, metadata and error handling
kernel/pci.c          PCI configuration-space discovery
kernel/rtl8139.c      Polling Ethernet driver with RX ring and four TX DMA buffers
kernel/net.c          ARP/IPv4, DHCP, ping, DNS and bounded TFTP client
kernel/net_wire.c     Bounded wire codecs shared with sanitizer tests
kernel/programs.c     Installs embedded /bin programs into RAMFS
user/                Freestanding NASM programs, syscall macros and linker script
tests/smoke.py        Headless QEMU functional tests
tests/storage.py      Persistence, host FAT inspection and fault-injection tests
tests/network.py      Real QEMU network and deterministic Ethernet fault fixture
tests/net_wire_test.c Sanitized packet decoder checks and fuzzing
tools/netfiles.py     Preserve/create local TFTP example files
tools/mkdisk.py       Exclusive-create FAT32 image formatter; preserves existing files
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
