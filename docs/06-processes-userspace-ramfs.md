# Processes, Ring 3, ELF and RAMFS (v0.6)

Historical milestone: v0.7 adds VFS and persistent storage; see
[the storage design](07-vfs-ata-fat32.md) for current filesystem behavior.

The kernel can now load independently linked i386 programs, run them in
Ring 3 with private physical memory, preempt them, and service checked file
and console system calls. This is a bounded teaching kernel; it has no disk
driver, network stack, SMP or production security claim.

## Context switching and privilege changes

The GDT includes DPL 3 code/data descriptors (`0x1B`/`0x23`) and a 104-byte
32-bit TSS (`0x28`). TSS.ss0 is the kernel data selector. Before restoring a
user process, the scheduler sets TSS.esp0 to its private 8 KiB kernel stack.
The I/O bitmap starts beyond the TSS limit, so all user port I/O is denied.
Every initial user IRET frame sets IF and clears IOPL. CR0.EM/TS disable
unimplemented floating-point/MMX contexts; SSE is not enabled either.

On IRQ, exception or `int 0x80`, the common entry saves the same 56-byte
`struct regs` as before. A user entry additionally has old ESP/SS after it.
The C handler returns a frame pointer in EAX. Assembly loads that pointer
into ESP before restoring segments, `popa`, and `iret`. Choosing another
saved frame switches both stack and register context. Kernel stack frames
and user IRET frames deliberately have different tails.

The PIT remains at 100 Hz. The scheduler has one kernel task (PID 0) and at
most seven user tasks. The quantum is five timer ticks, round robin. Timer
interrupts wake sleeping tasks using wrap-safe signed deadline comparison.
CPU ticks and context selections are observable in `ps`. `yield`, sleep,
exit and user faults may switch immediately. PIC EOI is sent before switching.

Kernel code is non-preemptible by tasks. The shell task explicitly permits
scheduling only around its idle `sti; hlt; cli` path. Syscalls use an
interrupt gate, which clears IF, and run to completion. IRQ handlers do not
allocate or touch the filesystem. This prevents concurrent kernel allocator,
filesystem and task-table operations on a single CPU. This design must be
revisited before adding kernel threads, blocking syscalls, SMP or preemptible
kernel paths. `task_idle` is for task 0 only. Console output from background
programs may visually interleave with the shell prompt.

## Address spaces and reclamation

Each process has a static, page-aligned directory and one private user page
table. All processes share supervisor-only kernel low mappings and heap
tables. User memory occupies `0x40000000`–`0x403FFFFF`. ELF segments must end
by `0x40300000`; the top 16 KiB is a writable stack, with unmapped space
below it. All other user pages start absent. Frames are allocated from the
PMM and zeroed through a temporary supervisor mapping before publication.
Only the ELF loader uses that scratch mapping, in a non-preemptible kernel
path. A CR3 reload on every selection flushes process translations.

ELF text is user-readable and read-only; writable data is separate. With
32-bit non-PAE paging there is no NX bit, so writable data is still executable.
Rejecting ELF W+X segments does not provide hardware W^X. Kernel low mappings
remain broadly writable as in v0.5, but their U/S bit denies user access.

Exit/fault marks a task zombie and switches away first. Task 0 then closes
all descriptors and releases every private frame after changing CR3. Static
kernel stacks and page tables are reused; PIDs increase monotonically. The
zombie record remains visible in `ps` until its slot is reused. `kill 0` is
denied. Cancellation of foreground `run` uses Esc and the same kill/reap path.
There are no signals, parent/child trees or general waitpid API yet.

The heap also gained `heap_trim`: it keeps the last free block's header and
at least 16 payload bytes, unmaps whole trailing pages and returns their
frames. `kfree` retains pages unless trim is explicitly requested. Memory
stress tests trim after checking accounting, so a 2 MiB boot has enough RAM
left to run user processes.

## ELF loader

Only ELF32 little-endian i386 ET_EXEC is accepted. There are at most 16
program headers. Header sizes, program-header bounds, file ranges, virtual
ranges, alignment congruence, entry coverage, filesz <= memsz and page-level
segment overlap are checked before allocating. PT_INTERP/PT_DYNAMIC and W+X
load segments are rejected. Every load page is privately allocated and
zeroed, which supplies BSS and clears stale data. File data is copied through
the kernel scratch mapping even for read-only user text. The stack is then
mapped. Any allocation/copy failure releases all new user frames.

Programs have no argc/argv/environment or user heap API yet. Build the NASM
examples with `user/linker.ld` and `user/abi.inc`; Make embeds their ELF images
in `.rodata` and installs them as read-only RAMFS files. Adding a new example
requires listing it in Makefile, `boot/programs_blob.asm`, and
`kernel/programs.c`. General dynamically linked Linux executables will not
run against this ABI.

## System call ABI

Use `int 0x80`, number in EAX, arguments in EBX/ECX/EDX. EAX returns a result
or negative errno. Other general registers are preserved. User segments must
use the supplied flat layout; the common entry restores ES/FS/GS from DS.
Each I/O call is limited to 1024 bytes. A zero-byte I/O accepts any pointer
without dereferencing it. Nonzero buffers are checked page by page for user
presence and, for read destinations, write permission. Arithmetic wrap,
kernel pointers and unmapped spans are rejected. Path strings are copied
with a 64-byte bound. The non-preemptible kernel cannot unmap user buffers
between validation and copying.

| EAX | Call | EBX | ECX | EDX |
| --- | --- | --- | --- | --- |
| 1 | console print | buffer | length | — |
| 2 | exit | status (low 8 bits) | — | — |
| 3 | yield | — | — | — |
| 4 | getpid | — | — | — |
| 5 | ticks | — | — | — |
| 6 | sleep | ticks, up to INT32_MAX | — | — |
| 7 | open | path | flags | — |
| 8 | read | fd | writable buffer | length |
| 9 | close | fd | — | — |
| 10 | file write | fd | readable buffer | length |
| 11 | seek (absolute) | fd | offset | — |

Open flags: 0 = read, 1 = read/write, 2 = create, 4 = truncate; create/truncate
require read/write. v0.8 adds 8 = exclusive creation (requires create); an
existing name returns EEXIST 17 without truncation. Common errno values: EPERM 1, ENOENT 2, ESRCH 3, E2BIG 7,
ENOEXEC 8, EBADF 9, EAGAIN 11, ENOMEM 12, EFAULT 14, EBUSY 16, EINVAL 22,
EMFILE 24, ENOSPC 28, EROFS 30, ENAMETOOLONG 36, ENOSYS 38. Shell errors print
the positive errno number; user code receives the negative value.

User exceptions terminate that process with status `128 + vector` and report
PID, EIP, error code and (for #PF) CR2. Kernel exceptions still halt the
machine, except the existing harmless kernel breakpoint self-test.

## RAM filesystem

There are 24 nodes (increased from 16 in v0.9 for the new user programs) in an
absolute-path flat namespace, with names up to 63
characters. This is not a directory hierarchy: path components label files,
and parent directories are not materialized. Relative paths, dot components
and ambiguous traversal names are rejected. Built-in ELF images and
`/etc/motd` are read-only. Mutable files hold at most 4096 bytes each. Each
process has eight descriptors with independent offsets; the open reference
count prevents deleting an open file. Exit closes even forgotten descriptors.
Seek past EOF is allowed on writable files; subsequent writes zero-fill the
gap. Oversized writes fail atomically. Contents are reset on reboot.

Shell commands: `ls`, `cat <path>`, `write <path> <word>`, `rm <path>`,
`run <path>`, `spawn <path>`, `ps`, `kill <pid>`, and `trim`, alongside the
existing memory commands. `write` accepts one word in this shell revision.
`cat` recognizes ELF files and directs the user to run/spawn instead.

## Programs and validation

| Program | Purpose |
| --- | --- |
| `/bin/hello` | Ring 3 execution, getpid, open/read/print/close |
| `/bin/probe` | Actual CPL check, bad pointers/spans, read-only destinations, errno, BSS/stack, yield |
| `/bin/filedemo` | Create/write/seek/read/compare; leaves fd open to test exit cleanup |
| `/bin/sleep` | Tick comparison verifies delayed wakeup |
| `/bin/spin` | Infinite CPU loop without syscalls; proves timer preemption; kill or Esc |
| `/bin/isolate` | Two-second PID sentinel at the same virtual data address in each process |
| `/bin/crash` | Read supervisor memory; #PF error 5, exit 142 |
| `/bin/readonly` | Write user code; #PF error 7, exit 142 |
| `/bin/guard` | Read below the user stack; #PF error 4, exit 142 |
| `/bin/io` | User port I/O; #GP, exit 141 |

Boot self-tests cover RAMFS offsets/EOF/limits/open-file deletion and ELF
validation plus allocation rollback. The synthetic large ELF load succeeds
then frees its frames on larger RAM, and exhausts/reclaims partial allocations
at 2 MiB. Existing PMM/paging/heap tests still run.

`make test` uses QEMU TCG and PS/2 input at 2, 64 and 128 MiB. Every run tests
user programs, file syscalls, failure containment, kernel NULL fault reporting
and exact frame reclamation. The 64 MiB run adds two non-yielding tasks whose
tick/switch counts must both increase, two simultaneous private-address-space
sentinels, seven-task exhaustion, kill/reap, repeated slot reuse and foreground
Esc cancellation. It retains all previous memory and shell regression checks.
Only three completed PASS lines establish suite success. No test requires KVM,
a graphical display, third-party Python packages or a network service.
