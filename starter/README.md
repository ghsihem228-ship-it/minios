# Step 1: a separate C/x86_64 kernel that prints Hello

This directory is an independent restart following the requested incremental
build order. Assumptions: **C11, x86_64, QEMU TCG, NASM/GNU Make** on Ubuntu/WSL.
C is chosen to reuse the current toolchain; Rust `no_std` is a reasonable
alternative with stronger language-level safety, but would require a different
runtime/build setup. The current MiniOS i386 kernel is still built from the
repository root. Run this starter's commands **from this directory**.

Only boot and serial output are implemented. Scheduler, handles, capabilities,
stable user syscalls, POSIX, transactions and typed pipes are later milestones.
There is no user ABI to freeze in step 1.

## Alternatives and recommendation

| Boot route | Benefit | Cost/limit |
| --- | --- | --- |
| Multiboot v1 with QEMU's direct loader | Few dependencies, explicit load bounds, easy serial/GDB loop | Starts in 32-bit mode; requires a small transition to x86_64; no UEFI/USB image |
| UEFI or a bootloader such as Limine | Better starting point for modern firmware, framebuffer and richer boot metadata | Adds firmware/loader integration and an artifact/version to manage |

Use direct Multiboot for this first emulated milestone. Reconsider the loader
before targeting real hardware; loader details should remain internal rather
than become part of a public application ABI. The `.bin` uses the documented
Multiboot explicit-address flag; QEMU loads it at 1 MiB. `kernel.elf` is kept for
symbols, not passed to `-kernel`: QEMU's legacy Multiboot path does not accept
an ordinary ELF64 kernel in the same way it accepts ELF32.

For output, polling a UART is simpler before an IDT exists. An interrupt-driven
UART avoids busy waiting and supports concurrent buffered I/O, but needs IRQ
setup, synchronization and queue policy. Choose bounded polling for step 1.

## Layout and build

```text
starter/
  Makefile
  linker.ld
  boot/entry.asm       Multiboot header, CPU checks, 32-to-64-bit transition
  kernel/main.c        serial_init(), Hello, halt
  kernel/serial.h      kernel-only UART interface
  kernel/serial.c      COM1 port I/O with bounded transmit waits
  tests/test_boot.py   actual QEMU serial/CPU-mode checks
```

On Ubuntu/WSL, required packages are:

```bash
sudo apt update
sudo apt install build-essential binutils nasm qemu-system-x86 python3
```

From the repository root:

```bash
cd starter
make
make test
make run
```

Expected serial output:

```text
Hello
```

`make run` uses `qemu-system-x86_64`, the emulated `qemu64` CPU, TCG, 64 MiB,
COM1 on standard output, no GUI, no network and no disk. The VM stays halted
with maskable interrupts disabled after printing. Exit with Ctrl+C. `make clean`
removes only `starter/build/`. No existing MiniOS disk image is attached.

## Concrete boot and serial contract

`boot/entry.asm` expects the Multiboot protected-mode contract: EAX contains
`0x2BADB002`, paging is off, and data segments are flat. The header contains
physical load/entry/BSS bounds. The bootstrap checks CPUID availability and
long-mode support before touching EFER, explicitly clears BSS, creates static
page tables, enables PAE/LME/paging and far-jumps through its own GDT.

The initial tables map the first 1 GiB with supervisor-only, writable 2-MiB
identity pages. They are the **minimum bootstrap machinery needed to execute
64-bit C**, not a frame allocator, virtual memory manager or complete paging
policy. No page is allocated dynamically, and the Multiboot memory map is not
yet consumed. The linker keeps this starter's image/tables/stack below 2 MiB.

At the call to `kernel_main`, the CPU is in 64-bit ring 0, the kernel has a
16-KiB stack with RSP aligned to 16 bytes before CALL (8 modulo 16 at C entry),
DF is clear, and maskable interrupts remain disabled. C uses the System V AMD64
calling convention. No libc is linked, and red-zone, stack protector, x87/MMX/
SSE generation and PIE/PIC are disabled. Floating-point state is not managed.

The internal UART interface is:

```c
void serial_init(void);
int serial_write(const char *text);
```

COM1 is port `0x3F8`, 115200 baud, 8N1, FIFO enabled, UART interrupts disabled.
`serial_write` accepts a valid kernel NUL-terminated string, emits CRLF for LF,
and returns 0 if the UART fails to become transmit-ready within a bounded
poll. The current use is single-CPU and single-writer; this is not a future
user syscall or a concurrency-safe logging subsystem. `main.c` is intentionally
small:

```c
#include "serial.h"
_Static_assert(sizeof(void *) == 8, "This starter requires x86_64");
void kernel_main(void) {
    serial_init();
    (void)serial_write("Hello\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
```

Bootstrap failures before C can run have fixed serial messages for invalid
Multiboot entry, missing CPUID and unsupported long mode. These messages are
not the later structured error-chain design.

## Validation and debugging

`make test` uses only the Python standard library and QEMU. It checks the flat
image's header checksum/load bounds, boots the actual image at 16/64/128 MiB,
requires exactly `Hello\r\n` on COM1, and queries CPU registers through QMP.
It verifies EFER.LME/LMA, CR0.PG, CR4.PAE, CR3 alignment and EFLAGS.IF clear.
A separate Pentium III CPU case must report `Boot error: long mode unsupported`
before entering 64-bit code. Every test has a deadline and terminates its own
VM. Reboot/fault loops cannot satisfy the exact single-message assertion.

No fake UART unit test is needed for these small direct I/O routines: the
functional test exercises the real emulated UART. There is not yet a useful
untrusted-input parser to fuzz. Fuzzing becomes useful for boot metadata,
interrupt/syscall boundaries and object/IPC parsing when those are introduced.
Do not add random-input tests with no meaningful invariant merely to claim
coverage.

Optional GDB workflow (install `gdb` separately):

```bash
make debug
```

In a second terminal in this directory:

```bash
gdb build/kernel.elf
```

Then in GDB:

```gdb
target remote 127.0.0.1:1234
hbreak kernel_main
continue
info registers
```

The debug target stops at reset and exposes GDB only on loopback. Its initial
firmware execution is not yet 64-bit; the breakpoint is at the later C entry.
GDB is optional and was not available in the cloud validation instance; CPU
mode checks there use QMP instead.

## Risks and common mistakes

- Enabling interrupts without an IDT can turn an interrupt/fault into a reset.
  There is no exception handler yet. NMI/fault handling is also unfinished;
  `cli` masks only ordinary maskable interrupts. Step 2 must address this.
- Wrong Multiboot load/BSS bounds, page-table alignment, GDT code flags, CR3,
  EFER ordering or stack alignment can cause a silent triple fault.
- The identity mapping is intentionally broad and writable/executable. There
  is no W^X, NX policy, null-page guard, capability enforcement or user isolation
  in this starter. Do not call it a secure finished kernel.
- The UART transmit poll is bounded by iterations, not elapsed milliseconds.
  A failed UART prevents reliable serial diagnostics. There is no flow control,
  receive path, multi-writer lock or normal shutdown protocol.
- Host glibc, PIE, red-zone use, implicit SIMD or unsupported compiler-runtime
  helper calls can break a freestanding kernel. Build with the provided flags;
  keep the ELF for debugging rather than booting it as this flat image.
- This is tested in QEMU TCG with a legacy PC UART/Multiboot loader. No UEFI,
  bootable ISO/USB, real hardware, SMP or production security claim is made.

Stop here. Step 2 (interrupts and CPU exception handling) starts only after an
explicit `next` request.
