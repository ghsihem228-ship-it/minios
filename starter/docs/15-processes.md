# Isolated preempted user processes

## Alternatives, trade-offs and recommendation

Running every program through the original global user arena is small, but one
program cannot remain alive while another executes and process state is shared.
Running each process in a kernel thread with its own suspended syscall stack
scales to blocking services, but adds kernel-stack lifetime, preemption, locks and
nested context-switch hazards before the services are ready for them.

Use a bounded process dispatcher with **private CR3 roots** now. A timer interrupt
saves the complete user interrupt frame and returns to the dispatcher; it chooses
one ready process in round-robin order. Kernel code remains non-preemptible and
syscalls complete without blocking. The shell calls the dispatcher between input
polls, so CPU-bound programs cannot monopolize interaction. It runs one CPU;
concurrency means interleaved execution, not parallel CPUs. Blocking IPC and
kernel-thread integration remain later work.

Private page tables cost four 4-KiB frames per process and a CR3 reload per slice.
There is no PCID or global TLB optimization. This is preferable to remapping a
shared user arena on every switch: frame ownership and isolation are explicit,
and a failed load never modifies another process's mappings.

## Concrete interfaces and data ownership

Working C/assembly: `kernel/process.c`, `vmm.c`, `services.c`, `shell.c`,
`boot/user.asm`, and CPU-bound test fixture `user/concurrent.asm`.

```c
#define USER_PROCESSES 4u
enum user_state { USER_FREE, USER_READY, USER_RUNNING, USER_EXITED };
struct user_info {
    uint64_t pid, slices;
    int64_t result;
    enum user_state state;
};
struct user_space {
    uint64_t tables[4];       /* owned PML4, PDPT, PD and PT frames */
    uint64_t saved[16];       /* this process's COW checkpoint entries */
    int snapshot;
};
int64_t user_spawn(const void *elf, size_t size, struct ramfs *fs,
                   cap_rights_t explicit_rights, uint64_t *pid);
void user_poll(void);
int64_t user_kill(uint64_t pid);
int64_t user_reap(uint64_t pid, int64_t *exit_code);
int user_info(unsigned slot, struct user_info *out);
```

These are **trusted kernel interfaces**, not new user syscalls. Raw PIDs do not
grant a program authority over another process: user code has no PID-based kill
or spawn syscall. The debug shell has launch/control authority. A later public
process ABI should use capability handles to process objects, with rights for
wait/control and explicit startup grants, rather than expose raw kernel pointers.
The existing eighteen syscalls and ABI version remain unchanged.

Each process owns its handle table, error chain, transaction checkpoint, saved
CPU frame, 512-byte FXSAVE image, user frames and four page-table frames. Reusing
a slot preserves handle generation history. PIDs increase monotonically, never
reuse a prior value, and fail closed on counter exhaustion. Zero is never a PID.
Four live or unreaped processes exhaust the pool; allocation failure publishes
no child and restores all acquired handles/frames.

The user arena is still `0x40000000..0x40010000`: at most fourteen image pages,
a guard page, and one stack page. ELF validation enforces read/execute or
read/write/NX mappings. Every process can use identical virtual addresses while
owning different physical pages. All kernel low-memory tables and the kernel
arena remain shared supervisor mappings. The new private PDPT's user branch is
entry 1; kernel entry 0 retains its supervisor protection. Kernel-only CR3 has
no user mappings. Switching back to it precedes releasing user frames or tables.

`pmm_free_table` accepts only FRAME_TABLE ownership; callers must first detach
all table references and flush the address space. No kernel shared table is freed
by process teardown. Failed loads free partial table allocations and partially
mapped payloads. Zero-filled fresh user frames prevent reading a previous owner.

## Scheduling and CPU-state invariants

```text
FREE --successful load--> READY --dispatch--> RUNNING
RUNNING --timer slice--> READY
RUNNING --exit/fault/watchdog--> EXITED --reap--> FREE
READY --trusted kill--> EXITED
```

At each ~100 Hz timer delivery from CPL3, the handler saves all fifteen GPRs,
RIP/RSP/CS/SS/RFLAGS and returns to a trusted host continuation. The PIC EOI occurs
before this transition. The dispatcher saves x87/MMX/XMM0–15 and MXCSR with
FXSAVE64 and restores the next process's image with FXRSTOR64. Fresh images have
empty x87 registers, zero payloads and masked/default control values. The kernel
is compiled without floating-point/SIMD generation. CR0 MP/NE and CR4 FXSR/SIMD
exception support are enabled; EM/TS are cleared. OSXSAVE and FSGSBASE are disabled:
AVX/extended state and user-set FS/GS bases are unsupported. FS/GS selectors reset
on dispatch; there is no TLS ABI. This assumes the x86_64/QEMU target's FXSR/SSE2
baseline, not arbitrary pre-x86_64 CPUs.

Only one process executes at a time. INT80 is an interrupt gate with IF clear;
no syscall yields or retains a kernel stack. A single guarded 16-KiB TSS entry
stack is reused **only after the preceding syscall/interrupt fully returns**.
No process-owned kernel stack can be freed while executing on it. Exit/fault/kill
release mappings, snapshots and handles before the zombie's small result record
is reaped. The kernel resumes with its own CR3, syscall gate DPL0 and RSP0 cleared.
Do not add blocking syscalls or enable kernel preemption without revisiting this
stack and locking design.

`user_poll` gives one ready process one timer slice, then returns to the shell.
The next call advances the cursor. Shell polling/HLT time is additional overhead;
this is not a real-time latency or proportional CPU scheduling guarantee.
Foreground `run` dispatches the other ready processes while waiting. Its own
program is terminated after 200 delivered slices; wall time increases with
competition. Background `spawn` has no ordinary runtime deadline and ends through
exit, fault or `kill`. These controls are debug-shell policy, not a resource quota
system for an untrusted multiuser environment.

## Transactions under concurrency

COW metadata, the saved BEGIN frame and its FXSAVE checkpoint belong to the
process. Rollback restores memory, staged file changes/offsets, GPR control flow,
and the saved x87/XMM environment. The shared RAMFS overlay still allows only one
transaction globally. Once BEGIN succeeds, the dispatcher keeps resuming its
owner until commit/abort/exit/fault, without running another process or the shell.
A separate 200-tick transaction watchdog bounds a CPU-bound background transaction.
This avoids exposing tentative data or rolling back another writer's offsets.
It can pause interaction for roughly two seconds; it is not concurrent database
isolation. Future service transactions need owner-aware namespaces and conflict
semantics rather than holding this global scheduling exclusion.

## Interactive usage

```text
spawn probe-a
spawn probe-b
ps
spawn spin
ps
kill 17
run demo
```

Use a PID actually printed by your `spawn`/`ps`, not the example 17. `probe-a`
and `probe-b` continuously verify private memory, GPRs, x87 control/data, MXCSR,
and XMM registers without yielding. `ps` reports delivered CPU slices. Background
exit notifications include a result and preserve a partially typed command.
The shell reaps jobs automatically. Four background jobs leave no slot for a
foreground `run`; kill a job first. Embedded fixtures are still the only programs.

Build embeds ELF copies stripped of DWARF debug sections to retain the 2 MiB boot
budget. Full debug user ELF files remain in `build/user-N.elf` for inspection.
The i386 implementation at the repository root is unchanged.

## Validation and security limits

```bash
make test-processes
make test
```

Boot checks run two actual CPU-bound isolated probes, confirm private markers
and increasing counters, kill/reap independently, and require frame counts to
return to baseline. At 2/16 MiB they additionally exhaust physical memory with
zero through six free frames, exercising every partial table/payload allocation
failure for a three-page probe. A failed spawn must publish PID zero and leak
nothing. Host ASan/UBSan's independent PMM ownership model now includes freeing
table frames and rejection of unaligned/free/reserved/non-table releases.

QEMU tests at 16/64 MiB type through emulated UART while background programs run,
freeze actual CPL3 execution, walk the active CR3 to distinct physical data pages,
check code/data/stack permissions and supervisor kernel leaves, verify CPU progress,
fill/reuse the pool, reject stale/overflowed PIDs, recover from user faults, run
transaction demos alongside other jobs, terminate a CPU-bound background
transaction through its watchdog, verify file rollback, and check complete frame
reclamation.
The existing demo also checks that rollback restores XMM state and x87 control.
Original multi-RAM, exception, syscall, shell and disk tests remain required.

This is not SMP, kernel preemption, a thread API, user process-handle ABI, fork,
blocking wait/sleep, TLS, XSAVE/AVX support, user-service IPC, or a complete POSIX
process model. Resource policy, transient-execution mitigations and formal
isolation proofs remain work. Interrupt delivery and hardware state must be
reviewed again before adding new CPU features or arbitrary third-party binaries.
