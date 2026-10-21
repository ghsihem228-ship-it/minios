# 18. Waiting for multiple service events

## Design choices

Polling one handle at a time wastes CPU and makes it difficult to notice a child
exit while waiting for its reply. A persistent wait-set object scales better to
large servers but adds registration, removal and lifetime rules. For the current
four-process system, a bounded array passed to a single syscall is simpler.

The chosen implementation accepts up to eight entries and returns an index bitmap.
It does not require a writable result buffer when waking a process, so wakeup
cannot fail due to copy-on-write allocation or a stale userspace pointer. Callers
can poll an indicated object to obtain its exact signal bits when needed.

## ABI v1.4 specification

```c
#define SYS_WAIT_MANY 24
#define WAIT_ITEMS 8
struct wait_item { uint64_t handle, signals; };
/* INT80: RDI=items pointer, RSI=count, RDX=timeout_ticks; other args zero.
 * Return RAX=status, RDX=ready-index bitmap (zero on failure). */
int mini_wait_many(const struct wait_item *items, size_t count,
                   uint64_t timeout_ticks, uint64_t *ready);
```

Each entry requires WAIT authority. Signal masks follow OBJECT_WAIT: EXITED for a
process; READABLE or PEER_CLOSED for a read endpoint; WRITABLE or PEER_CLOSED for a
write endpoint. Count must be 1..8. Zero timeout polls (EAGAIN if none ready),
UINT64_MAX waits indefinitely, and other durations must be at most UINT64_MAX/2.
Finite expiration returns ETIMEDOUT. Any ready entry completes the wait; bit i is
set if entry i has at least one requested signal. Readiness wins over expiration.
All currently ready entries are reported. Duplicate entries are allowed and each
gets its own result bit. This is wait-any, not wait-all.

The kernel copies descriptors, validates every entry even if an earlier entry is
ready, and pins the objects. A bad later entry must release every earlier pin.
No caller pointers are retained. On timeout, successful wakeup or process death,
all pins are released. Killing a blocked process also closes its ordinary handles.
A self-process entry is rejected. Active transactions reject WAIT_MANY with EBUSY.
Signals remain level-triggered; readiness does not reserve data for the caller.

## Implementation and tests

`kernel/process.c` owns the bounded wait record and scheduler wakeup path.
`user/mini.c` supplies the wrapper. `user/supervisor.c` waits for either a service
reply or child exit, while `user/ipc_service.c` waits for requests or peer closure.
They execute in separate ring-3 address spaces, including the cancellation test.

```bash
make test-ipc
make test
```

Tests exercise all eight ready bits, duplicate entries, mixed ready/unready sets,
polling, finite timeouts, bad pointers/counts/deadlines, invalid later entries,
missing WAIT rights, simultaneous process-exit/EOF, actual asynchronous replies,
infinite service waits and process termination while pins are held. Repeated runs
verify pool reuse and unchanged free-frame counts, including competing CPU jobs.

## Limits and pitfalls

One blocking operation per process, eight entries, single CPU. Scheduling scans
bounded records rather than maintaining interrupt-safe event queues; wakeups occur
when the dispatcher runs. A leaked endpoint or process capability can keep a peer
alive. Duplicating an endpoint into this wait array also extends its lifetime
until the wait ends. This mechanism grants no new authority and does not implement
runtime capability transport, a service directory or a userspace device driver.
