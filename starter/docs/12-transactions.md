# Step 12: single-level COW process undo and RAMFS write staging

> Historical milestone: [milestone 15](15-processes.md) supersedes the single-runner
> scheduling and CPU-state limits described here.

## 1. Alternatives

Eager memory/file copying makes rollback simple but costs the full working set at
begin. Page-level copy-on-write makes begin cheap in bytes and copies only first
writes, but requires fault-aware ownership and TLB handling. A filesystem journal
can protect disk metadata but does not rewind CPU registers or memory. Full-system
VM snapshots cover external resources differently and are not process transactions.

## 2. Recommendation

Use one active snapshot for the single user runner: preserve user PTEs and the
syscall frame, clear writable bits, and copy writable pages on first write.
Stage file writes in a private RAMFS image copied lazily on first mutation. Restore
file offsets too. Reject handle-table/namespace/pipe mutations during a transaction
so undo does not promise to rewind already-delegated authority or external effects.
This is rollback of memory plus existing-file changes, **not an installer undo
system**, durable transaction manager, or checkpoint of arbitrary kernel resources.

## 3. Specification and invariants

```text
12 TX_BEGIN(directory_with_CONTROL) -> value 0 at initial return
13 TX_COMMIT()                     -> 0
14 TX_ROLLBACK()                   -> resumes TX_BEGIN with value 1
```

All other arguments are zero. Only the explicitly supplied filesystem root is
eligible; possession of a FILE handle alone does not authorize whole-namespace
snapshots. Begin fails if already active. A saved interrupt frame records the exact
user register/stack/RIP state at BEGIN; rollback restores it and memory so it returns
from that same call again. Normal code must distinguish its value 0 versus 1.

```c
struct syscall_result tx = syscall_invoke(SYS_TX_BEGIN, root, 0, 0, 0, 0, 0);
if (!tx.status && !tx.value) {
    /* Mutate existing file contents / memory. */
    syscall_invoke(SYS_TX_ROLLBACK, 0, 0, 0, 0, 0, 0);
    /* Successful rollback never returns here. */
}
/* After undo, tx.value == 1 and pre-BEGIN memory/file offsets are restored. */
```

Each original mapped page is retained logically by the snapshot. On first user
write fault to an originally writable page, allocate/copy one 4096-byte frame and
replace the live PTE; the original remains snapshot-owned. Kernel copyout explicitly
prepares the same COW path, avoiding a supervisor fault on a readonly user PTE.
Commit frees displaced originals and restores write bits. Rollback frees new pages
and restores original PTEs; every replacement invalidates the relevant TLB entry.
PMM FRAME_MAPPED denotes both current mappings and snapshot-retained ownership
while active; it is not a general physical reference counter. No second alias or
second process shares those frames. Nested transactions are rejected.

RAMFS keeps baseline image/revision and saved offsets for sixteen descriptions.
First write copies the image into its fixed overlay. Reads/writes then use that
view. Commit checks the baseline revision and publishes the overlay; an injected
conflict returns ECONFLICT and leaves the transaction available to abort. Rollback
discards the overlay and restores offsets. Only one runner executes and no kernel
task is scheduled during it, so other clients cannot observe the staged view.
There is no multi-client isolation protocol; adding concurrent clients requires
per-transaction view selection and locking/version rules before enabling them.

Allowed calls while active: ABI/error queries, existing-file read/write/seek,
commit/rollback, and exit. Handle close/clone/restrict, open/unlink, pipe creation/
send/receive and another begin fail EBUSY. Unknown calls still return ENOSYS.
Exit, fault or timeout automatically aborts a pending transaction before closing
handles and freeing pages. External device/network effects are not present and
would need explicit rejection or compensating semantics in a future design.

## 4. Working code and cost

kernel/vmm.c implements PTE snapshots/COW; fs.c implements lazy image staging;
services.c saves/restores the syscall frame and enforces allowed operations;
process.c aborts before cleanup. There is no placeholder rollback syscall.

Begin scans at most 16 PTEs and sixteen offsets, copying only metadata. First
memory write per page costs one frame plus a 4-KiB copy and TLB invalidation.
At most 16 extra frames could be required; code/guard pages are not writable, so
actual use is lower. The fixed RAMFS overlay reserves another image (about 17 KiB)
per namespace; the first write copies the whole image. This is whole-image lazy
copying, not per-file/page filesystem COW. Commit/abort walk sixteen entries.
Allocation failure on a COW fault terminates/aborts the user rather than corrupting
the original. Copyout returns EFAULT for an invalid destination and ENOMEM if it cannot
allocate a required COW page.

## 5. Tests

The real ring-3 demo modifies stack memory and an existing file, rolls back to
BEGIN, verifies the original value/content/offset, then performs and commits a
second transaction. A separate process exits with a pending write; kernel checks
unchanged baseline contents and no frame/refcount leak. Host tests verify staged
bytes stay out of the baseline, rollback/commit and injected revision conflict.
The full VM suite repeats runs across RAM sizes and inspects final page ownership.

## 6. Risks and limits

No handle/namespace creation rollback, persistent disk atomicity, crash recovery,
shared memory, multithread checkpointing, nested snapshots, global revocation,
network undo, rollback of console output, or arbitrary installation is supported.
A user can branch on rollback's return value; documentation must prevent infinite
undo loops. This facility is explicit and narrow; a general "undo process/install"
product feature needs much more resource and isolation policy before release.
