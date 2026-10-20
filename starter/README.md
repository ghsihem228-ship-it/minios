# MiniOS64: active x86_64 kernel

This 64-bit kernel boots in QEMU, runs capability-scoped code in actual ring 3,
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

`make run` starts the interactive serial shell. `make run-window` opens QEMU's
VGA window (a graphical environment is required). Click the window to type;
Ctrl+Alt+G releases captured input. The keyboard uses a US layout.

```text
x64> help
x64> write note hello
x64> cat note
x64> save
x64> run demo
x64> spawn spin
x64> ps
```

Both run targets create a dedicated 1 MiB disk at `disks/minios64.img` if absent.
Existing images are preserved. Files change in RAM until `save` succeeds; the
latest valid snapshot is restored on reboot. `make clean` preserves the disk.
This is a private snapshot format, **not FAT32**; do not attach a physical disk
or the older i386 image. `disk` reports whether saving is available. Unknown or
unrecoverably corrupt formats are refused. Without a disk the shell still works.

On Windows, install native QEMU using the [QEMU Windows download links](https://www.qemu.org/download/#windows).
Build in Ubuntu/WSL, then copy these files to `C:\minios-run`:

```bash
mkdir -p /mnt/c/minios-run
make disk
cp build/kernel.bin disks/minios64.img run-windows.ps1 run-windows.cmd /mnt/c/minios-run/
```

Copy the image only on initial setup; preserve the Windows copy after saving
files there. Double-click `run-windows.cmd`, or run from PowerShell:

```powershell
& C:\minios-run\run-windows.ps1
# If QEMU is installed in a custom directory:
& C:\minios-run\run-windows.ps1 -Qemu 'D:\tools\qemu\qemu-system-x86_64.exe'
```

The helper discovers QEMU or reports that it is missing; Ubuntu's QEMU is a
separate installation. Native Windows launching has not been tested here.
The spin-program watchdog adds about two seconds per boot. Close the QEMU window
to stop the GUI guest; Ctrl+C stops the terminal guest. `make debug` binds a GDB
stub to loopback port 1234; use `build/kernel.elf` for symbols. Boot with
`build/kernel.bin`, whose Multiboot header supports QEMU's direct kernel loader.

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
| 14 | [Interactive console and durable file snapshots](docs/14-console-storage.md) |
| 15 | [Isolated, timer-preempted user processes](docs/15-processes.md) |

## Tests and source layout

```bash
make test            # complete QEMU regressions plus all host sanitizer/model tests
make test-extended   # new host parser/storage tests and actual ring-3 integration
make test-processes  # concurrent ring-3 programs, live CR3 checks, kill and cleanup
make test-host       # all host tests; builds its own user ELF fixture
make -j2 test        # parallel compilation, same validation
```

The complete suite boots at 2/16/64/128/512 MiB; verifies IDT/TSS/PIC, page-table
ownership and kernel protections; exercises real CPU faults, NMI, boot metadata
and module reservation; and checks that process cleanup frees frames and handles.
It runs ten C user ELF fixtures plus two CPU-bound assembly probes, repeating
the main demo. Those cover actual
CPL3 syscalls, denied kernel/code writes, NX stack, denied port I/O/privileged INT,
spinning timeout, zero-authority launch and pending-transaction abort. Kernel tasks
also exchange typed messages through explicitly delegated read/write endpoints.

Host ASan/UBSan tests include 100,000 memory-map cases, 50,000 frame-owner model
operations, 100,000 handle cases, 100,000 two-table capability model operations,
100,000 scalar-dispatch cases, and 100,000 ELF/wire mutant rounds and 100,000 snapshot parser mutants. These are
reproducible bounded tests, not proofs or coverage-guided fuzz campaigns.

```text
boot/                bootstrap, interrupt stubs, context switches, user entry/blob
kernel/              memory, tasks, handles/rights, errors, VFS/RAMFS, ELF, services
user/                freestanding CRT, ELF linker script, POSIX subset and demos
tests/               QEMU/QMP checks and sanitizer/model harnesses
docs/                design/specification for each milestone, current syscall ABI
```

## Boundaries that matter

- Single CPU, four isolated user processes with timer preemption and x87/XMM
  state preservation. Kernel tasks remain trusted/cooperative. There is no SMP,
  thread API, TLS or AVX support.
- RAMFS has explicit whole-filesystem disk snapshots. Moving filesystems,
  networking and most drivers to user services is still needed for the hybrid goal.
- The shell runs embedded ELF programs; there is no runtime disk exec or general Unix binary support.
  POSIX is a small source-level subset, not conformance or a complete libc.
- Transactions undo memory and existing-file writes/offsets. They reject namespace,
  handle-table and pipe effects while active; they do not implement installer undo,
  durable disk transactions, external-effect reversal or nested checkpoints.
- Typed pipes are bounded/nonblocking. No EOF signaling, blocking wait or user-process
  handle-transfer transport exists yet. Kernel launch grants provide explicit transfer.
- ABI v1.2 currently defines eighteen calls. Do not invent dozens of placeholder calls
  merely to hit a target count. Extending/finalizing the full public ABI needs real
  services and compatibility review.

The remaining work toward the full OS is tracked in [the roadmap](docs/roadmap.md).
