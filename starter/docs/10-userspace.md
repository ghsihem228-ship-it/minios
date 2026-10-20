# Step 10: validated ELF64 and real ring-3 execution

> Historical milestone: [milestone 15](15-processes.md) supersedes the single-runner
> scheduling and CPU-state limits described here.

## 1. Alternatives

Flat binaries need little validation but have no reliable segment permissions.
ELF64 supplies segments and an entry point at the cost of hostile length/offset
parsing. Shared-address-space multi-task execution is cheap but cannot isolate
processes from each other. Separate CR3 roots support concurrent processes but add
page-table lifecycle and scheduler complexity. A bounded single-user runner can
verify the actual privilege boundary before building a full process scheduler.

## 2. Recommendation

Use static ET_EXEC ELF64 with explicit W^X, a dedicated syscall stack, and one
user context at a time. The same CR3 contains supervisor kernel leaves and one
bounded user arena; every user page is removed/zero-reallocated between runs.
Kernel tasks do not schedule while the runner is active. This is an isolated
single-user execution harness, not concurrent multi-process support or a shell.

## 3. Specification and invariants

```c
struct elf_plan { uint64_t entry; unsigned count; struct elf_segment segments[8]; };
int elf_validate(const void *, size_t, struct elf_plan *);
int64_t user_run(const void *elf, size_t, struct ramfs *, cap_rights_t initial_rights);
```

The pure validator accepts ELFCLASS64, little endian, version 1, machine 62,
ET_EXEC, 64-byte header and 56-byte program headers (1..8). It rejects dynamic
loaders/interpreters/TLS, truncated and overflowing ranges, overlapping segment
pages, W+X, missing READ, empty loads, unsupported flags, non-page-aligned loads
and entries outside executable file bytes. Alignment must be zero or a power of
two <=4096. Sections/debug data are not loaded. The test toolchain emits compatible
static ELF; Linux binaries/PIE/dynamic libraries are not supported.

Arena: 0x40000000..0x4000ffff (16 pages). ELF uses at most pages 0..13, page 14 is
an unmapped stack guard, page 15 a zero-filled RW/NX stack. Segment BSS is zeroed.
Readonly executable pages are RX, writable pages RW/NX, kernel leaf permissions
remain supervisor-only. Parent U bits are set only to allow traversal to user
leaves; they do not grant access to supervisor kernel leaves. Physical identity
aliases stay supervisor RW/NX. A dedicated 16-KiB RSP0 stack has an unmapped guard.

User code/data selectors are 0x2b/0x33. Trusted assembly enters via IRETQ with
RFLAGS=0x202 (IF set, IOPL=0), clears other GPRs and supplies only the explicitly
granted directory handle in RDI (zero if no namespace was granted). Initial RSP
is 16-byte aligned; user/start.asm calls C using the SysV ABI. No argc/argv/envp
or TLS/FPU/SIMD context is provided. INT80 becomes DPL3 only during the runner,
then returns to DPL0; existing other IDT gates remain DPL0.

Syscall 6 EXIT(code) exits with the low 8 bits. Kernel-selected saved host RSP/CS/
SS/RIP are restored to a fixed return trampoline, never supplied by the user.
User faults end the run with `-1000-vector`; NMI, double fault and machine check
still halt the kernel. A delivered-tick budget of 200 terminates a non-yielding
user with -2000. This is an experimental watchdog, not a real-time guarantee.

Copyin/out accepts only the fixed user arena, checks addition overflow, presence
and write permission for every touched page, and prepares COW pages before output.
The arena has no huge pages and is not concurrently remapped. Those constraints
make the check-and-copy lifetime safe here; general SMP copyin needs more work.
Page tables are retained after first creation; user frames and handle references
must return to baseline on each subsequent run. Launches preserve token-generation
history, clear errors and start with only the explicitly supplied capability.

## 4. Working code and build

kernel/elf.c, process.c, user_vm.h and vmm.c contain loading and memory support;
boot/user.asm is entry/return. interrupts.c configures RSP0 and handles user faults.
user/start.asm, linker.ld, mini.c and demo.c build actual freestanding ELF binaries.
They are embedded at build time; there is no runtime exec syscall or disk loader.

```bash
make
make test-extended
make debug
# In another terminal: gdb build/kernel.elf
```

## 5. Tests

QEMU runs the feature demo twice, kernel-write, code-write, port-I/O, NX-stack and
privileged-INT fault programs, a spinning timeout, a pending-transaction exit,
and a zero-authority launch. Cleanup is checked against free-frame/refcount
baselines. Invalid syscall destinations include kernel, readonly code and a range
crossing the arena end. The host ELF validator runs 100,000 mutated/truncated inputs
under ASan/UBSan against the real generated executable.

## 6. Risks and limits

No concurrent user processes, fork, signals, ASLR, dynamic linker, system call
restart, general demand paging, FPU/TLS support or production exception recovery
is supplied. Sequential reuse of one arena does not establish separate concurrent
address spaces. Trusted kernel tasks retain arbitrary authority. No machine-check
or NMI recovery is claimed. The syscall stack and supervisor page permissions are
actual protection; stack canaries and test success are not proofs of security.
