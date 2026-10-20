# C/x86_64 experimental kernel: milestones 1–13

This independent starter boots in QEMU, runs capability-scoped code in actual ring 3,
and exercises structured errors, VFS/RAMFS, a small source-level POSIX library,
copy-on-write memory undo with staged file writes, and typed message pipes.
The repository root still builds the older **i386 MiniOS**; run all commands below
from **starter/**. These are working, tested starter implementations for each
milestone, not a completed general-purpose OS.

## Build and run

Ubuntu/WSL requirements: GCC/binutils, NASM, Make, Python 3 and QEMU x86.

```bash
sudo apt install build-essential binutils nasm qemu-system-x86 python3
cd minios/starter
make
make test
make run
```

In the prepared cloud workspace, select the supplied NASM/QEMU tools first:

```bash
export PATH=/workspace/.minios-tools/bin:$PATH
cd /workspace/minios/starter
make test
```

`make run` is a headless serial boot/self-test demonstration, **not an interactive
shell**. After the original milestone PASS lines, expect:

```text
PASS extended: errors, RAMFS, ELF64/ring3, POSIX subset, COW undo, typed pipes
READY: interrupts enabled; idle
```

Silence after READY is normal. The spin-program watchdog intentionally adds about
two seconds per boot. Ctrl+C stops QEMU. No disk, GUI, host network, KVM or external
service is required. `make clean` removes only starter/build. `make debug` stops at
reset and binds the GDB stub to loopback port 1234; use build/kernel.elf for symbols.
Do not boot the ELF directly: QEMU -kernel uses build/kernel.bin's Multiboot header.

## Current milestones and guides

Each guide explains alternatives/trade-offs, recommendation, data/ABI invariants,
working source, tests and risks. Guides 1–7 describe their historical milestone;
the later guides and [current ABI](docs/abi-v1.md) supersede earlier API versions.

| Step | Working implementation / guide |
| --- | --- |
| 1 | [Boot and serial](docs/01-boot.md) |
| 2 | [Interrupts, PIC/PIT, exceptions and guarded IST stacks](docs/02-interrupts.md) |
| 3 | [Memory manager](docs/03-memory.md): physical frame ownership and protected 4-KiB mappings |
| 4 | [Cooperative kernel scheduler](docs/04-scheduler.md): yield, sleep, exit and reaping |
| 5 | [Typed reference-counted objects and generation-checked handles](docs/05-handles.md) |
| 6 | [Scalar INT80 syscall foundation](docs/06-syscalls.md) |
| 7 | [Capability rights and explicit task grants](docs/07-capabilities.md) |
| 8 | [Structured cause chains](docs/08-errors.md) |
| 9 | [VFS interface and scoped volatile RAMFS](docs/09-vfs.md) |
| 10 | [ELF64 validation/loading and ring-3 execution](docs/10-userspace.md) |
| 11 | [Small POSIX source-compatibility library](docs/11-posix.md) |
| 12 | [COW memory rollback and staged existing-file writes](docs/12-transactions.md) |
| 13 | [Typed, bounded message pipes](docs/13-typed-pipes.md) |

## Tests and source layout

```bash
make test            # complete QEMU regressions plus all host sanitizer/model tests
make test-extended   # new host parser/storage tests and actual ring-3 integration
make test-host       # all host tests; builds its own user ELF fixture
make -j2 test        # parallel compilation, same validation
```

The complete suite boots at 2/16/64/128/512 MiB; verifies IDT/TSS/PIC, page-table
ownership and kernel protections; exercises real CPU faults, NMI, boot metadata
and module reservation; and checks that process cleanup frees frames and handles.
It runs nine built user ELF fixtures, repeating the main demo. Those cover actual
CPL3 syscalls, denied kernel/code writes, NX stack, denied port I/O/privileged INT,
spinning timeout, zero-authority launch and pending-transaction abort. Kernel tasks
also exchange typed messages through explicitly delegated read/write endpoints.

Host ASan/UBSan tests include 100,000 memory-map cases, 50,000 frame-owner model
operations, 100,000 handle cases, 100,000 two-table capability model operations,
100,000 scalar-dispatch cases, and 100,000 ELF/wire mutant rounds. These are
reproducible bounded tests, not proofs or coverage-guided fuzz campaigns.

```text
boot/                bootstrap, interrupt stubs, context switches, user entry/blob
kernel/              memory, tasks, handles/rights, errors, VFS/RAMFS, ELF, services
user/                freestanding CRT, ELF linker script, POSIX subset and demos
tests/               QEMU/QMP checks and sanitizer/model harnesses
docs/                design/specification for each milestone, current syscall ABI
```

## Boundaries that matter

- Single CPU and one user runner at a time. Kernel tasks remain trusted/cooperative;
  there is no concurrent per-process CR3 scheduler, SMP or FPU/TLS context support.
- RAMFS is a kernel reference backend and loses data at reboot. Moving filesystems,
  networking and most drivers to user services is still needed for the hybrid goal.
- No interactive shell, runtime exec, disk filesystem or general Unix binary support.
  POSIX is a small source-level subset, not conformance or a complete libc.
- Transactions undo memory and existing-file writes/offsets. They reject namespace,
  handle-table and pipe effects while active; they do not implement installer undo,
  durable disk transactions, external-effect reversal or nested checkpoints.
- Typed pipes are bounded/nonblocking. No EOF signaling, blocking wait or user-process
  handle-transfer transport exists yet. Kernel launch grants provide explicit transfer.
- ABI v1.2 currently defines eighteen calls. Do not invent dozens of placeholder calls
  merely to hit a target count. Extending/finalizing the full public ABI needs real
  services and compatibility review.
