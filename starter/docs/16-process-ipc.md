# 16. Process capabilities and blocking service IPC

## Design and recommendation

A global PID interface is convenient for Unix compatibility but needs a separate
access policy on every operation. A process object fits our existing capability
model: possession plus WAIT or CONTROL determines access. Use process handles
for userspace; retain PIDs only in the trusted debug shell.

Blocking inside C would require a private suspended kernel stack per process and
careful cancellation/unwind rules. Instead, save the user interrupt frame and a
bounded wait record, then return to the dispatcher. The next dispatch checks
readiness and deadlines before restoring the caller. This uses the existing
single entry stack and single-CPU, nonpreemptible kernel invariant. It is not SMP
safe without a new synchronization design.

One pipe object cannot tell whether its last reader or writer disappeared.
Separate reference-counted endpoint objects share a bounded queue. Duplication,
launch grants and wait pins preserve their endpoint's lifetime. Queued data drains
before EOF; closing all readers makes subsequent sends fail with EPIPE.

## Concrete contract

The exact scalar signatures and numbers are in [ABI v1.3](abi-v1.md). Calls 18–23
provide spawn, status, kill, wait, yield and sleep. A startup grant is:

```c
struct launch_grant {
    uint64_t source; /* handle in parent's table, requiring TRANSFER */
    uint64_t rights; /* subset of source rights */
};
```

SPAWN copies at most 16 KiB of ELF and eight grants before changing address spaces.
It creates a private address space, installs attenuated handles and returns a
WAIT|CONTROL|DUPLICATE|TRANSFER process capability. Failure releases every partial
resource. The child receives RDI=0, RSI pointing to its private handle-token vector,
RDX=vector length. No namespace root or parent authority is inherited implicitly.
The vector is mutable ordinary memory; changing a token cannot create authority.

Processes have FREE, READY, RUNNING, EXITED and WAITING states. A live managed
child owns a scheduler reference. On exit, memory and handles are reclaimed and
that reference drops. Process capabilities retain its status and pool slot until
the final reference closes. Closing the last capability while the child is alive
does not kill it. Status requires WAIT, kill requires CONTROL, and kill cancels
pending waits. Self termination uses EXIT. Fault results are -1000-vector; killed
processes report -2001. Ordinary EXIT keeps the low eight bits.

Wait records hold an object reference, requested signals, start tick and duration.
WAIT checks level-triggered EXITED, READABLE, WRITABLE or PEER_CLOSED. Zero duration
polls; UINT64_MAX waits indefinitely; finite durations are at most UINT64_MAX/2.
Readiness wins at the deadline, otherwise ETIMEDOUT. Sleep uses a timer-only record.
Readiness does not reserve a message: shared endpoints must retry EAGAIN. A writer
should request WRITABLE|PEER_CLOSED. Read EOF is success with zero frame bytes.
Only requested bits are returned; invalid or wrong-direction masks are rejected.

## Working code and tests

`kernel/process.c`, `kernel/pipe.c` and `user/mini.c` implement the contract.
`user/supervisor.c` launches `user/ipc_service.c` with only two endpoint grants;
they exchange typed u64 requests and replies in separate ring-3 address spaces.
Run the service with:

```text
x64> run ipc
```

```bash
make test-ipc
make test
```

Host ASan/UBSan exercises 1000 endpoint lifecycles, drain/EOF, EPIPE, duplicate and
grant lifetime, readiness and partial-create rollback. Real QEMU tests at
2/16/64 MiB check parent/child exchange, denied authority, invalid pointers,
partial grant/output-handle failure, timeouts, cancellation of sleeping and
pipe-blocked children, orphan completion, status lifetime and frame reclamation.
Competing non-yielding CPU probes must continue while the service runs.

## Limits and risks

There are only four process slots, four pipes and 32 handles per context. Retained
exit handles consume slots; live orphan processes need an explicit supervisor
policy. No process-group kill, multi-object wait, runtime handle attachment,
thread/TLS API or per-principal quota exists. Global exhaustion is still possible.
A foreground program waiting forever also holds the debug shell foreground; its
CPU-slice watchdog does not measure blocked wall time. Read/write calls remain
nonblocking; callers compose them with WAIT. Public children can use granted
filesystem objects but cannot begin the trusted-root transaction protocol.
Transactions reject all new process/wait operations. Moving actual drivers and
filesystem policy into services remains separate work.
