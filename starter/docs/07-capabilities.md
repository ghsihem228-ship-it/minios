# Step 7 only: capability rights and explicit task grants

## 1. Alternatives and trade-offs

| Decision | Alternative A | Alternative B | Choice here |
| --- | --- | --- | --- |
| Authority model | User IDs/ACLs checked through ambient global names | Private handle table entries naming objects and carrying rights | Handle capabilities; no global name lookup is added |
| Rights location | Store one permission mask on the object | Store a separate mask on each handle | Per-handle masks let two holders have different authority |
| Delegation | Copy all handles automatically or accept arbitrary destination IDs | Explicit, attenuated grants in a trusted launch path | Start empty; only a supplied launch list grants access |
| Revocation | Parent/child derivation tree with recursive revoke | Independent reference-counted entries with local restriction/close | Independent entries now; precise local semantics, no claim of global revocation |
| Token format | Signed/random bearer tokens usable across address spaces | Existing slot/generation tokens interpreted in private tables | Keep local tokens; use a trusted transfer path rather than serializing numbers |

UID/ACL systems can express policy, but checking a global path implicitly gives
callers access to a namespace. A capability instead names the object and operation
authority already granted to that caller. Broker/launcher policy is still needed
to decide who gets an initial capability; capability machinery cannot decide user
intent by itself. An object-global mask cannot express independent read-only and
read/write holders without extra indirection.

Revocation trees can withdraw descendants, but require ancestry lifetime tracking,
bounded traversal or lazy checks, synchronization and semantics for already-open
operations. This milestone favors independent references. Do not market local
restriction or parent close as revocation of existing descendants.

## 2. Recommendation and scope

Add explicit rights to every handle entry, require the complete requested mask at
typed lookup, and allow only subset derivation. Separate copying inside a task
from delegation to another task. Initialize tables empty; provide a trusted task
creation API which installs a grant list before publishing the task runnable.
Rollback all partial grants if any one fails.

The current ring-0, shared-address-space tasks are trusted. This is a working
capability mechanism and policy contract, **not isolation against malicious
programs**. A ring-0 caller can still modify memory, call the trusted mint API or
read devices directly. Ring-3 isolation and a safe userspace entry remain later
work; INT80 stays DPL0. FILE/SOCKET/etc. remain type tags, not implemented services.
READ/WRITE/WAIT/CONTROL are checked by generic lookup and test fixtures; actual
resource operations must apply the appropriate checks when they are implemented.

## 3. Concrete specification and invariants

`kernel/capability.h` defines a 64-bit rights mask shared with the scalar ABI:

```c
typedef uint64_t cap_rights_t;
#define CAP_READ      (UINT64_C(1) << 0)
#define CAP_WRITE     (UINT64_C(1) << 1)
#define CAP_WAIT      (UINT64_C(1) << 2)
#define CAP_CONTROL   (UINT64_C(1) << 3)
#define CAP_DUPLICATE (UINT64_C(1) << 4)
#define CAP_TRANSFER  (UINT64_C(1) << 5)
#define CAP_ALL (CAP_READ | CAP_WRITE | CAP_WAIT | CAP_CONTROL | CAP_DUPLICATE | CAP_TRANSFER)
struct handle_slot {
    struct object *object;
    uint32_t generation;
    cap_rights_t rights;
};
```

| Right | Meaning |
| --- | --- |
| READ | Future read/receive or a concrete object's documented read operation |
| WRITE | Future write/send or a concrete object's documented write operation |
| WAIT | Future wait/observe operation |
| CONTROL | Future documented mutating control operation; not a privilege override |
| DUPLICATE | Derive a same-table handle, retaining or reducing its rights |
| TRANSFER | Grant a handle into a destination chosen by trusted kernel launch/IPC policy |

Zero rights are valid. The holder can always close or reduce its own handle;
close/restriction do not require CONTROL. CAP_ALL is an explicitly requested known
mask, not a default and not a promise to include rights introduced in the future.
Unknown bits are rejected. No superuser bit bypasses the subset rule.

Private kernel APIs (these signatures supersede the historical step-5 signatures):

```c
enum object_status handle_install(struct handle_table *, struct object *,
                                  cap_rights_t initial, handle_t *);
enum object_status handle_lookup(struct handle_table *, handle_t,
                                 enum object_type, cap_rights_t required, struct object **);
enum object_status handle_clone(struct handle_table *, handle_t,
                                cap_rights_t requested, handle_t *);
enum object_status handle_duplicate(struct handle_table *, handle_t, handle_t *);
enum object_status handle_restrict(struct handle_table *, handle_t, cap_rights_t requested);
enum object_status handle_grant(struct handle_table *source, handle_t,
                                struct handle_table *destination, cap_rights_t requested,
                                handle_t *);
struct task_grant { handle_t source; cap_rights_t rights; };
int task_create_with_caps(void (*entry)(void *), void *argument,
                          const struct task_grant *, size_t count, handle_t *child_handles);
```

1. `handle_install` is trusted minting. It requires the caller already own a live
   object reference and explicitly choose valid rights. It is never exposed to
   arbitrary syscall arguments. The private API can mint authority; only trusted
   creators/brokers/launchers may use it. Old test fixtures now spell out CAP_ALL.
2. Lookup first rejects an unknown required mask, then validates the local token,
   then requires `(held & required) == required`, then checks the object type and
   acquires a reference. Permission failure returns OBJ_DENIED, leaves output
   NULL, and changes no count. Combined READ|WRITE requires both, not either.
   A zero-mask lookup is internal reference/type inspection, not permission to
   perform operations. Trusted callers must request the operation's full mask.
3. Clone/duplicate require CAP_DUPLICATE. Clone's requested mask must be a subset
   of the source. Duplicate preserves the exact existing mask and cannot restore
   dropped bits. A duplicate without DUPLICATE cannot itself be duplicated.
4. Grant requires CAP_TRANSFER and a subset mask; it adds a separate destination
   reference without removing the source. There is no implicit consumption/move.
   DUPLICATE and TRANSFER are independent; a transfer-only handle can be delegated
   by trusted policy without first cloning it. Dropping TRANSFER prevents onward
   delegation through this API; dropping DUPLICATE prevents local copying.
5. Restrict requires only possession, accepts a subset (including zero), and
   changes just that entry. It cannot restore a bit. It does not change the
   underlying object, previous duplicates, delegated entries or acquired pins.
   Closing a parent likewise does not revoke a child's independent reference.
6. All denied operations leave rights, table contents and reference counts
   unchanged. Clone/grant failure zeros its output. Unknown masks return
   OBJ_INVALID; a missing right or attempted amplification returns OBJ_DENIED.
   Capacity/refcount overflow retain their existing failure behavior.
7. Handle numbers are table-local. Equal numbers can name unrelated entries in
   two tables. Passing a number as task argument data is not a grant. Only a
   trusted installation of an entry in the recipient table conveys authority.
   There is no syscall selecting a recipient by guessed task slot or raw pointer.
8. Ordinary task_create inherits nothing. With-caps accepts at most 32 grants,
   selects each source from the current task's table and grants before setting
   READY. On any grant failure it clears partial entries, zeros all output tokens,
   leaves the task slot FREE and returns -1. No partial child can execute. Source
   handles are unchanged. Generation history is preserved even on rollback.
9. Launch arrays and outputs are trusted, valid kernel storage and must not alias
   each other, live tables or objects. For valid count/pointers, outputs are zeroed
   even on later failure. Invalid count or missing array returns -1 before array
   access; there is no output-validity promise for an invalid array. Entries may
   be repeated deliberately. Caller must deliver the resulting child-local tokens
   to its entry argument/startup data; it must not substitute parent-local tokens.
10. Operations are nonblocking/non-yielding in one-CPU task context, or current
    nonblocking syscall context with IF clear. Destructors retain step-5 rules.
    No IRQ/NMI mutations, SMP races, asynchronous revocation or user copy are
    supported. Prior lookups pin objects; local restriction is not cancellation.

On x86_64 a slot is now 24 bytes and a 32-entry table 768 bytes. Eight tables use
6144 bytes, 2048 bytes more than step 5. Lookup/restrict are O(1), clone/grant scan
at most 32 entries, and a launch list of N grants costs at most N such scans plus
rollback. Each successful derivation adds one object reference, no heap object.

Scalar ABI additions, **major 1, minor 1**:

| Number | Call | Arguments | Success value |
| --- | --- | --- | --- |
| 0 | ABI_QUERY | required major | `(1 << 32) \| 1` |
| 1 | HANDLE_CLOSE | local handle | 0 |
| 2 | HANDLE_DUPLICATE | local handle | same-rights local copy; requires DUPLICATE |
| 3 | HANDLE_RESTRICT | local handle, new rights | 0 |
| 4 | HANDLE_CLONE | local handle, requested rights | attenuated local copy |

Numbers 0..2 and their register layouts are retained. A newly restricted handle
can now deny duplicate as required by capability policy. New calls reserve args
2..5 as zero; old calls still reserve args 1..5 as zero. Unknown masks return
SYS_EINVAL (-2); missing authority/amplification returns new SYS_EACCESS (-7).
All failure values remain zero. Version-aware clients must inspect major/minor,
not require equality with the historical 1.0 value. The cause-chain design is
step 8, so no public error strings or structured errors are added here.

## 4. Working code and build

```text
kernel/capability.h         rights constants, shared scalar mask
kernel/handle.h, handle.c   per-entry authority and checked derivation
kernel/scheduler.h, .c      atomic launch grants and cleanup integration
kernel/syscall_abi.h       v1.1 scalar constants
kernel/syscall.c           restriction/clone dispatch and denial mapping
kernel/capability_test.c    shared exhaustive kernel/host rights tests
tests/test_capability.c    independent two-table model fuzzing and syscall checks
docs/07-capabilities.md    this guide
```

Example of trusted launcher use with an already-owned parent handle:

```c
struct task_grant grant = { .source = parent_handle, .rights = CAP_READ };
handle_t child_token;
int slot = task_create_with_caps(child_entry, &child_token, &grant, 1, &child_token);
/* Keep child_token storage alive until child_entry has consumed it.
 * The task argument is opaque data; it does not alias grant source storage.
 * A successful return has published the child and initialized its token. */
```

The real compiled launcher/destructor fixtures are in scheduler.c. A child cannot
write, duplicate or transfer a READ-only grant through the checked APIs. It can
close it freely. This is the mechanism to build a later scoped Documents service:
there is no Documents filesystem object or path policy implemented in this step.

Run inside `starter/`:

```bash
make
make test-capabilities
make test
make run
```

The prepared cloud host uses:

```bash
export PATH=/workspace/.minios-tools/bin:$PATH
```

Normal boot adds this before READY:

```text
PASS capabilities: rights, attenuation, delegation, launch rollback
READY: interrupts enabled; idle
```

## 5. Tests and fuzzing

Shared kernel/host tests exercise all 4096 source/request mask pairs for both
same-table clone and cross-table grant, compare against an independent subset
rule, and verify delegated READ/WRITE access and exact reference accounting.
They cover zero authority, missing delegation bits, unknown masks, local
restriction, denied restoration, and continued access through a prior copy.

The host harness runs 100,000 deterministic operations over two tables with an
independent model of each live token's rights, checking mint, clone, grant,
restriction, lookup and close, failed-output clearing and the total reference
count after every operation. Trusted fixture minting re-seeds authority only to
exercise more states; no such mint syscall exists. ASan/UBSan cover the actual
manager sources. Tests also check scalar syscall denial and reserved arguments.

QEMU checks include real INT80 CLONE/RESTRICT and EACCESS responses; ordinary
scheduler workers verify empty tables on task-slot reuse. A real child receives
only READ, rejects WRITE and duplicate, runs after parent close, and drops its
remaining reference at exit. A two-entry launch list fails its second grant:
partial entries must be closed, output tokens cleared and no child published.
The full regression suite retains all earlier protected-memory/CPU/fault checks.

One fixed random seed plus exhaustive masks is not a proof. Future tests should
cover live ring-3 isolation, IPC consent/transfer, confused-deputy brokers,
resource-specific checks and concurrent close/revoke when those features exist.
No untrusted application sandbox or coverage-guided fuzz result is claimed here.

## 6. Risks, pitfalls and limits

The main risk is confusing an in-kernel authority mechanism with a hardware
security boundary. Ring-0 tests can bypass every check. Kernel installation and
zero-required-mask lookup are privileged building blocks, not safe user APIs.
Every future object operation must check its own mask before dereferencing state.
No rights check should silently fall back to ambient credentials or global paths.

Never use a caller-selected task/table pointer, regenerate wider masks on duplicate,
automatically inherit all parent entries, compare numbers across tables, or let a
broker act with its own broader capability without validating the caller's request
and destination. TRANSFER must not become permission to grant into any victim task:
future IPC needs an authorized destination and receiver policy/consent.

Restriction is not transitive revocation. If users need an uninstall/permission
withdrawal button to stop prior descendants, implement revocable indirection or
another explicit revocation mechanism before making that promise. Already-pinned
operations and external side effects also need cancellation semantics.

Rights express operation authority, not quotas, time limits, namespace scope,
object identity authenticity across machines or control over covert channels.
A READ bit on a scoped directory service will require service-side child-open
rules; a global pathname plus READ does not scope Documents access. These services,
user-mode isolation, resource quotas and capability-aware IPC remain later work.

Stop at step 7. Wait for `next` before structured error chains.
