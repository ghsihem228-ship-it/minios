# Step 4 only: cooperative kernel scheduler (C / x86_64)

## 1. Alternatives and trade-offs

Cooperative round robin switches at explicit calls. Its switch saves the SysV
callee-saved registers and stack pointer; ordinary C calls already account for
caller-saved registers. Interrupt handlers run to completion. This is easy to
inspect and has little synchronization machinery, but a looping task can starve
all other tasks. It cannot enforce CPU fairness against untrusted programs.

Timer-preemptive round robin switches at interrupt return after a time slice.
It can run non-yielding tasks fairly, but must retain complete interrupt state,
control kernel preemption, protect shared state, and eventually preserve FPU/TLS
and address-space state. Adding a stack switch inside an arbitrary C timer
handler without designing its entry/return contract is unsafe.

For stack allocation, static stacks give predictable storage and no allocator
failure path. Page-allocated stacks support scalable counts and unmapped guard
pages, but require integration with the memory-manager API. Step 3 supplies a
page allocator; static stacks keep this first scheduler
independent of allocation failure and cleanup paths. Guarded dynamic stacks
are a later improvement to this scheduler.

## 2. Recommendation

Use cooperative, single-CPU round robin with seven static worker stacks for this
increment. It is a working scheduler foundation, not a preemptive process
scheduler. Timer IRQs advance sleep deadlines; they never switch tasks. Keep
bootstrap as an always-runnable idle context. This implementation uses the
existing step-3 mappings and adds no later milestone. It changes only the
x86_64 starter; root MiniOS is separate.

Each stack is 16 KiB: seven stacks reserve 112 KiB of BSS. Each scheduling decision
scans at most eight records, so it is O(N), with N fixed at eight. Wakeup latency
is at least the requested delivered-tick interval, plus time until another
scheduling point. With a non-yielding task, latency is unbounded.

## 3. Concrete internal specification

These are private kernel functions, not syscalls. Step 4 introduces no public
ABI, handles, processes or user entry.

```c
void scheduler_init(void);
int task_create(void (*entry)(void *), void *argument);
void task_yield(void);
void task_sleep(uint64_t ticks);
_Noreturn void task_exit(void);
unsigned task_live_count(void);
```

Initialize exactly once after IDT/PIC/PIT initialization, with IF clear. Ordinary
scheduling calls are made from ring-0 task context with IF set, never from an ISR,
NMI, exception, or while holding a lock. Creation preserves incoming IF. Yield
internally preserves the caller's IF when that context resumes, but callers must
not rely on IF staying clear while other tasks run. A new task starts with IF set.

```c
enum state { FREE, READY, RUNNING, SLEEPING, ZOMBIE };
struct task {
    uintptr_t sp;
    enum state state;
    uint64_t since, delay;
    void (*entry)(void *);
    void *argument;
};
```

- Slot zero is bootstrap/idle, never sleeps or exits. Only one task is RUNNING.
- Creation accepts a non-null entry, initializes a FREE slot, then publishes it
  as READY with interrupts masked. It returns slot 1..7, or -1 for null entry or
  capacity exhaustion. This slot is not a durable identity and must not become
  an authorization token. Arguments must outlive their use by the task.
- RUNNING becomes READY on yield; scanning starts after the old slot, wraps,
  and selects the first READY task. The old task can be selected again.
- Positive sleep changes RUNNING to SLEEPING. Sleep zero is yield. Positive sleep
  requires a worker context, IF=1 and duration <= UINT64_MAX/2; misuse halts with
  a diagnostic. Deadlines use unsigned tick subtraction, including wraparound;
  the design assumes sleep checks occur within a tick-counter cycle.
- Returning from an entry is equivalent to exit. Exit marks ZOMBIE, switches
  away and never returns. Another task's scheduling pass changes it to FREE;
  creation wipes its stack before reuse. Never reuse the executing stack.
- Scheduling mutations and switching run with IF=0. CLI is sufficient only for
  this one CPU, not SMP, NMI synchronization, or untrusted ring-0 code.
- The switch saves RBP, RBX, R12..R15 and RSP. A return address supplies RIP.
  Per-call saved IF remains in each context's C stack frame. New stack frames
  provide SysV function entry alignment (RSP modulo 16 is 8). DF must be clear
  at every C call, as required by SysV.
- Compiler flags disable the red zone, x87 and SIMD use. No FPU/SIMD, TLS,
  CR3, FS/GS base or per-task privilege state is switched.
- Stack-bottom canaries detect some overruns at scheduling points. They do not
  prevent corruption and are not guard pages or a security boundary.

## 4. Working code and build

```text
starter/
  boot/switch.asm          SysV context save/restore
  kernel/scheduler.h      private task API
  kernel/scheduler.c      states, static stacks, round robin, tests
  kernel/main.c           initialization, self-test, bootstrap idle loop
  tests/test_scheduler.py QEMU integration test
  docs/04-scheduler.md    this specification
```

Complete, commented C and assembly are supplied in those files. The existing
interrupt dispatcher is unchanged. Its PIT count drives sleep checks.

From the extracted starter directory, with GCC/binutils, NASM, Python and QEMU
installed:

```bash
make
make test-scheduler
make test
make run
```

On this prepared cloud machine, first select the locally installed tools:

```bash
export PATH=/workspace/.minios-tools/bin:$PATH
```

The serial terminal prints the existing boot checks followed by:

```text
PASS scheduler: yield, sleep, exit, capacity, reuse
READY: interrupts enabled; idle
```

The bootstrap context then idles; there is no shell. Ctrl+C stops `make run`.

## 5. Testing

The boot self-test runs two actual worker contexts, checks separate stack-local
values, explicit yields/zero sleeps, twelve timed sleeps per worker, no early
wakeup, return-to-exit and explicit exit. It fills seven slots, rejects the eighth,
and repeats termination/reuse sixteen times. A tick deadline catches stalls when
timer interrupts still run; the host VM deadline also catches IF-disabled stalls.

`make test-scheduler` verifies exact successful serial output at 16/64/128 MiB
using real QEMU TCG guests. `make test` reruns the existing long-mode, GPR/IRQ,
IDT/TSS/IST, fault, NMI, PIC and unsupported-CPU checks; normal output now includes
the scheduler success line. These tests are not proof of arbitrary task safety,
and the original IRQ register test is not an exhaustive switch-register test.

Unit-test candidate for future growth: isolate the selection/wakeup policy from
CPU switching, then check no sleeping/zombie task is selected, circular ordering,
capacity, and tick wraparound. No host unit-test or fuzz result is claimed here.
A useful future fuzz target is a bounded reference-model sequence of create,
yield, sleep, ticks and exit; compare states and selection with the pure policy.
Do not fuzz arbitrary saved RSP values on a live kernel. No untrusted wire parser
or syscall boundary exists at this step.

## 6. Risks and common mistakes

All tasks run in the same ring-0 address space and can overwrite each other or
the kernel. Cooperative scheduling and canaries provide no isolation. A task can
starve peers, disable IRQs, overflow its stack or fault the whole machine. Static
stacks have no guard pages in this initial static-stack implementation.

Do not call these APIs in interrupt context, sleep with IF=0, retain a lock over
yield, reclaim the current stack, or hand out internal slot numbers as stable
handles. Do not use floating point, SIMD, custom FS/GS bases, SMP or per-task
address spaces with this switch. Compiler flags are part of its correctness.

PIT ticks are delivered interrupts, not wall-clock time: masked/coalesced ticks
lengthen sleeps. The bootstrap idle loop may wait one timer tick even when a
worker is ready; that is bounded here only because bootstrap keeps IF set and
PIT ticking. This is suitable for learning and trusted kernel tasks, not a
commercial desktop or a stable userspace scheduling ABI.

Stop at step 4. No handle manager or later milestone is introduced.
