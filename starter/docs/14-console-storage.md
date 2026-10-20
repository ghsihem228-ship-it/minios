# Interactive console and file snapshots

> Historical milestone: [milestone 15](15-processes.md) supersedes the single-runner
> scheduling and CPU-state limits described here.

## Alternatives and recommendation

For early interaction, UART-only I/O is simple and automatable but hides the
kernel inside a terminal. A framebuffer terminal scales to a desktop but needs
font rendering and a richer boot/display contract. Use VGA text plus UART on
QEMU's legacy PC now: the same output reaches both, with PS/2 or UART input.
This deliberately targets the emulated legacy PC, not arbitrary UEFI hardware.

For storage, porting FAT32 gives interoperability but brings allocation chains,
directories and interrupted metadata updates into this milestone. An append log
scales further but requires compaction and recovery policy. Use two fixed banks
of whole-RAMFS snapshots first. Space is bounded and recovery can independently
validate both copies. This is a private format, not a FAT32 implementation.

## Implementation and interface

Working C sources: `kernel/console.c`, `shell.c`, `block.c`, `store.c`, and the
portable `store_wire.c`. Public kernel interfaces:

```c
void console_init(void);
void console_clear(void);
void console_putc(char c);
int console_input(void); /* ASCII byte, or -1 when no input */
int64_t store_init(struct ramfs *fs);
int64_t store_save(struct ramfs *fs);
int store_ready(void);
uint64_t store_sequence(void);
```

There are no new syscalls. The trusted shell holds an explicitly granted root
handle; user programs keep the existing scoped launch grants. The shell invokes
existing embedded ELF fixtures, not arbitrary executable files. Storage is a
trusted kernel service for now; user programs cannot issue raw ATA commands.
`SYS_EIO=-15` distinguishes storage failure from invalid user pointers.

The 128-byte input buffer accepts 127 printable characters. Overflow discards the
entire command on Enter. Backspace edits a non-overflowed line. US set-1 scan
codes support Shift/Caps Lock; extended navigation, non-US layouts, mouse, Unicode,
history and quoting are not implemented. Input is polled on timer wakes; this is
not an interrupt-driven keyboard driver. VGA is supervisor-only, writable and NX.
UART timeout does not prevent VGA output. File contents escape control characters
by displaying a dot, except newline, so `cat` cannot inject terminal escapes.

Commands: `help clear version mem uptime ls cat write rm programs run disk save`.
Names are relative, 1–31 ASCII letters/digits/underscore/hyphen/dot; `.`/`..` and
slashes are forbidden. Eight files, 2048 bytes each. `write NAME TEXT` overwrites
from offset zero **without truncation**; remove and recreate to shorten a file.
`run spin` intentionally times out; faults terminate the fixture, not the shell.
No process can run concurrently with another user fixture yet.

## Disk format and invariants

`tools/mkdisk.py` creates a new dedicated sparse 1 MiB image using exclusive
creation. It never overwrites an existing file. The kernel never formats disks.
The image is a data disk; QEMU boots `build/kernel.bin` using `-kernel`.
All integer fields are unsigned little endian; sectors are 512 bytes.

| Location | Meaning |
| --- | --- |
| LBA 0 | `MOS64DSK` magic (8 bytes), version=1 (u32), bank sectors=64 (u32), bank count=2 (u32) |
| LBA 1 / 65 | Bank header: `MOS64FS1`, version u32, payload length u32, sequence u64, payload checksum u32, header checksum u32 |
| LBA 2 / 66 | 33 sectors containing the 16704-byte namespace payload, zero-padded final sector |

Checksums use FNV-1a32: payload checksum covers exactly 16704 bytes; header checksum
covers its first 28 bytes. Remaining header bytes are zero on write. Each of eight
2088-byte entries contains `used:u32`, `size:u32`, `name:[u8;32]`, `data:[u8;2048]`.
The decoder validates length, used flag, sizes, names, NUL padding and duplicate
names before modifying its destination. Unused nodes have size zero. Encoders
zero unused entries and bytes beyond EOF, avoiding disclosure of stale RAM.

Boot selects the valid bank with the greatest nonzero sequence. An empty formatted
disk has two zero headers; its first save is sequence 1. One corrupt bank can fall
back to the other. Both corrupt or unrecognized formats disable saving. The shell
remains usable with its initial RAM namespace. Equal sequences choose bank zero;
normal writes never produce ties. Sequence overflow rejects further saves.

Save writes the inactive payload, flushes, writes its checksummed header last,
then flushes again. It publishes the new selected bank/sequence only on success.
Any write/flush failure disables subsequent writes until reboot. Filesystem
transactions block snapshot save. ATA is primary-master LBA28 PIO, bounded polling,
with no DMA or interrupt handler. Unknown or corrupt disks must remain unchanged.

`save` is explicit: successful RAM writes and process transaction commits are not
persistent until it succeeds. This snapshots files only, not running CPU state or
COW process memory. Storage flush ordering depends on the device/backend honoring
flush. A reported I/O failure may still have changed disk bytes; inspect/reboot
rather than assuming the attempted snapshot did not reach disk.

## Build and validation

```bash
make run                 # serial input/output and dedicated disk
make run-window          # VGA window and keyboard
make test                # all boot/host/shell/storage checks
make test-shell          # QMP PS/2 input, VGA memory, UART socket input
make test-store          # reboot persistence and recovery on temporary images
```

Host ASan/UBSan exercises 100000 bounded snapshot mutants, including truncated
input, duplicate names and destination preservation on failure. QEMU tests verify
save/reboot, independently inspect both bank checksums/data, corrupt the newer
payload to recover the older copy, and verify corrupt/unknown images are unchanged.
Shell tests cover overflow, editing, scrolling, file operations, real ring-3
faults/timeouts and frame cleanup. Tests never open the user's persistent image.
Byte corruption is not a complete power-loss/fault-injection campaign. Native
Windows GUI launching is provided as a helper and has not been executed here.

## Risks and next work

FNV is a corruption detector, not authentication or encryption; malicious host
images can construct valid snapshots. The trusted shell has full namespace rights
and is a debugging interface, not an authenticated multiuser session. Bounded PIO
loops can still stall a single CPU and are unsuitable for high-performance I/O.
VGA/keyboard routines are single-CPU and not safe for concurrent console writers.
The fixed namespace is intentionally tiny; larger files need a different storage
layout and resource accounting. Snapshot redundancy is not a backup. More robust
storage needs injected read/write/flush failure tests and torn-sector testing.
