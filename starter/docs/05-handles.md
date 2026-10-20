# Step 5 only: kernel objects and handles

## 1. Alternatives and trade-offs

| Decision | Alternative A | Alternative B | Choice |
| --- | --- | --- | --- |
| Object representation | Independent structures and special lookup rules for every resource | Small common header embedded in resource-specific structures | Common header: one lifetime protocol, type-checked lookup |
| Reference representation | Expose pointers or plain slot numbers | Opaque slot plus generation token | Generations reject stale tokens and reveal no kernel pointer |
| Lifetime | Unique ownership or tracing garbage collection | Explicit reference counting | Reference counting fits shared handles and deterministic close; cycles need explicit design |
| Table storage | Dynamically grown hash/radix structure | Fixed per-task array | 32 entries per task, bounded scans and no allocator dependency |
| Concurrency | Atomic refcounts plus table locks | Serialized task-context operations | Current cooperative, single-CPU kernel permits operations with no yield and no IRQ callers |

A raw pointer is fast internally but cannot become an untrusted ABI token. A plain
index is compact but immediately aliases a different object after slot reuse.
Generation checking solves reuse within a table; it does not authenticate a token
or make it meaningful in another table.

## 2. Recommendation and boundary

Use one embedded object header, explicit creator/handle/lookup references, typed
lookup and a 64-bit local handle. Start every task with an empty table. Close all
its handles at exit, preserve table generations across task-slot reuse, and retire
a slot rather than wrap its generation. This is simple enough to audit now.

This implements the shared mechanism only. FILE/PROCESS/THREAD/SOCKET/DEVICE/TIMER
are reserved internal type tags; their concrete implementations do not exist yet.
OBJECT_TEST is the real test fixture. There are no generic read/write operations,
public syscalls, rights masks, handle inheritance/transfer, namespaces, user pointer
validation or capability enforcement in this step. Ring-0 tasks remain trusted.

## 3. Concrete specification and invariants

The complete working declarations are in `kernel/object.h` and `kernel/handle.h`:

```c
typedef uint64_t handle_t;
#define HANDLE_INVALID UINT64_C(0)
#define HANDLE_SLOTS 32u
struct object {
    uint32_t refs;
    enum object_type type;
    void (*destroy)(struct object *);
};
struct handle_slot { struct object *object; uint32_t generation; };
struct handle_table { struct handle_slot slots[HANDLE_SLOTS]; };
```

```text
handle bits 63..32: generation (1..UINT32_MAX)
handle bits 31..0:  index + 1 (1..32)
zero generation or out-of-range index: invalid
empty slot with zero generation: permanently retired
```

Internal signatures (not syscalls and not a frozen ABI):

```c
enum object_status object_init(struct object *, enum object_type,
                               void (*destroy)(struct object *));
enum object_status object_get(struct object *);
void object_put(struct object *);
void handle_table_init(struct handle_table *);
void handle_table_clear(struct handle_table *);
enum object_status handle_install(struct handle_table *, struct object *, handle_t *);
enum object_status handle_lookup(struct handle_table *, handle_t,
                                 enum object_type, struct object **);
enum object_status handle_duplicate(struct handle_table *, handle_t, handle_t *);
enum object_status handle_close(struct handle_table *, handle_t);
struct handle_table *task_handle_table(void);
```

Status values are OBJ_OK, OBJ_INVALID, OBJ_TYPE, OBJ_FULL, OBJ_OVERFLOW. They are
private constants, not step 8's structured error chains.

1. All pointers are trusted kernel addresses. Table init requires fresh, valid
   storage; it is called exactly once. Reinitializing a used table would revive
   old tokens and leak references. Clear closes entries without resetting history.
2. Object init validates the type and non-null destructor, and gives the creator
   one reference. The caller supplies storage, usually an enclosing structure
   with this header first. Never initialize a live object again. Caller-managed
   storage must outlive every reference; a handle does not copy a stack object.
3. Install borrows a live creator/lookup reference, takes one additional reference
   on success, and publishes an entry. Failure changes neither table nor count.
   Outputs are set to zero/NULL on failure when a non-null output is provided.
   Output pointers must refer to separate writable storage, not alias the table
   or object fields. Table parameters to init/clear must be valid and non-null.
4. Lookup checks index, generation, occupancy and exact type before returning an
   owned reference. The caller must put it. That pin survives handle close; an
   in-flight operation is therefore safe from concurrent logical close when
   preemption is eventually designed. Current operations themselves cannot yield.
5. Duplicate adds an independent same-table handle/reference. Closing either one
   does not close the other. There is no transfer to a second task or rights
   attenuation yet. Handle numbers are interpreted only in the supplied table;
   identical numeric handles may validly refer to different objects in two tasks.
6. Close first removes the entry, then increments generation (overflow yields
   retired zero), then drops its reference. An invalid or repeated close returns
   OBJ_INVALID and drops no reference. Destructors observe the old token invalid.
7. A reference increment at UINT32_MAX fails with OBJ_OVERFLOW. Put requires an
   owned reference; zero-count put is a kernel bug and traps on host / halts in
   kernel. Put at count one transitions to zero and invokes destroy exactly once.
   It never touches the object again because destroy may free its storage.
8. Destructors must not block, yield, resurrect the object, or install entries in
   a table being cleared. They may release references to other owned objects.
   Releasing cycles recursively will not collect them: prevent cycles, use weak
   references designed later, or add explicit cycle-breaking ownership rules.
9. Per-task tables are separate static storage from task records. Task creation
   has no inheritance. Both explicit exit and returning entry clear handles before
   stack reaping; task reuse retains generations. Bootstrap owns an empty table
   and never exits. Future forced termination must use this same cleanup path.
10. One CPU, cooperative task context, no ISR/NMI callers and no scheduling inside
    an operation/destructor is the synchronization contract. This is why refs
    are not atomic. CLI alone would not make this SMP-safe. Preemption requires
    a table-lock/refcount lifetime design before enabling it here.

At the current x86_64 ABI, the header is 16 bytes, each slot 16 bytes, a table
512 bytes and all eight task tables 4096 bytes. These are implementation sizes,
not promises about the future user ABI. A lookup/close is O(1); install/duplicate
and clear scan at most 32 slots. Destruction cost depends on the concrete object.

## 4. Working starter code and build

```text
kernel/object.h, object.c   type header and checked reference ownership
kernel/handle.h, handle.c   generation table, lookup, duplicate, close and drain
kernel/handle_test.c        actual kernel/host shared self-tests
kernel/scheduler.c          per-task tables and exit cleanup integration
kernel/main.c              boot self-test and success marker
tests/test_handle.c        host lifetime test and deterministic model fuzzing
```

Example ownership flow, using caller-owned persistent storage:

```c
struct object fixture;
handle_t h;
/* destroy_fixture is a real callback supplied by the concrete object owner. */
if (object_init(&fixture, OBJECT_TEST, destroy_fixture) == OBJ_OK) {
    if (handle_install(task_handle_table(), &fixture, &h) == OBJ_OK) {
        struct object *pin;
        if (handle_lookup(task_handle_table(), h, OBJECT_TEST, &pin) == OBJ_OK)
            object_put(pin);
        handle_close(task_handle_table(), h);
    }
    object_put(&fixture); /* creator reference */
}
```

The example is explanatory; the complete compiled fixture/destructor is in
`kernel/handle_test.c`. The real heap-lifetime example is in `tests/test_handle.c`.
There is no new kernel heap implementation in this milestone.

Run from `starter/` with the same tools as step 4:

```bash
make
make test-handles
make test
make run
```

On the prepared cloud host select its tools first:

```bash
export PATH=/workspace/.minios-tools/bin:$PATH
```

Normal boot adds this line after the scheduler success marker:

```text
PASS handles: types, references, stale tokens, capacity, retirement
READY: interrupts enabled; idle
```

## 5. Tests and fuzzing

`make test-handles` runs the actual manager sources under ASan/UBSan, then boots
QEMU at 16/64/128 MiB. Shared self-tests check duplication, wrong-type rejection,
pinning across close, double-close rejection, stale generations, capacity,
clear/reuse, generation exhaustion/retirement and reference overflow.

The host lifetime test allocates a containing object on the heap, closes its only
handle while a lookup reference exists, reads its payload, then drops the pin and
requires exactly one destruction. The deterministic 100,000-operation model test
tracks a separate live-token set across random install, close, lookup and malformed
64-bit tokens. It verifies reference counts after each operation and checks
remembered stale tokens. This is model fuzzing with one fixed reproducible seed,
not coverage-guided fuzzing or proof of safety.

The QEMU scheduler workers each install an object in their own table, repeatedly
yield/sleep and look it up, then leave the handle open at return/explicit exit.
Both objects must be destroyed by task cleanup. `make test` also runs the existing
memory sanitizer/model tests and all CPU, IRQ, protected-memory and fault cases.
Future fuzz extensions should explore multiple seeds, multiple object lifetimes,
destructor dependency graphs, full table mutation against an independent model,
and transfer/rights rules when those actually exist.

## 6. Risks, security pitfalls and mistakes

Handles are local references, not cryptographic secrets. Knowing another task's
number must never let a future syscall select that other task's table. Syscalls
must derive the current process/table from trusted scheduler state. None of that
boundary exists yet, and trusted ring-0 code can still corrupt any table directly.

Type tags prevent accidental casts through this API, not malicious memory writes.
Never cast a generic header without successful typed lookup and a live reference.
Do not expose pointers, forget a lookup put, free creator storage early, reset
generations on task reuse, or make the destructor sleep. A stale raw object pointer
cannot be validated safely by reading its reference count after it has been freed.
Reference counting is an ownership discipline, not a safe-pointer runtime.

Retiring exhausted slots prevents ABA revival but can eventually exhaust a table;
OBJ_FULL is deliberate. Reserved type tags and current enum values are not stable
public ABI commitments. Capability rights, IPC transfer, syscall validation,
structured errors and real file/process implementations belong to later steps.

Stop after step 5. Wait for `next` before the syscall layer.
