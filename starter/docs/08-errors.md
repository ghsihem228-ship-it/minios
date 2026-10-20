# Step 8: bounded structured cause chains

## 1. Alternatives

A single errno is cheap and portable but loses the causal operation and authority
requirement. A variable-size tree with kernel strings is expressive, but adds
allocation, lifetime, localization and information-disclosure problems at entry.
A bounded numeric chain gives deterministic storage and lets user libraries render
localized messages without receiving kernel pointers.

## 2. Recommendation

Use four fixed cause slots per execution context, numeric domains and a scalar
getter. Keep RAX status/RDX value compatible. On failure the adapter records the
chain before return. Successful ordinary calls clear it; ERROR_GET never overwrites
it, including when a getter request itself is invalid. Retrieve/copy it before
another ordinary call. This is deliberately last-error context, not a durable log.

## 3. Specification

```c
struct error_cause { uint32_t domain, code; uint64_t detail; };
struct error_chain { uint32_t version, count; struct error_cause causes[4]; };
void error_record(struct error_chain *, int64_t status, uint64_t operation, uint64_t required);
struct syscall_result error_field(const struct error_chain *, uint64_t index, uint64_t field);
```

Version 1 is 72 bytes including its 8-byte header, with 16-byte causes. Domain 1,
code 1 identifies operation by syscall number in detail. Domain 2 identifies the
positive status magnitude in code. On EACCESS, domain 3/code 1 records the full
required rights mask in detail. That mask describes a requirement which failed,
not a claim that every listed bit is absent. There are two causes normally and
three on authority failure, in outer-operation-to-underlying-reason order.

Syscall 5, ERROR_GET(index, field), returns domain for field 0, code for field 1,
detail for field 2. ERROR_GET(0,3) returns cause count. Remaining args are zero.
Invalid index/field returns EINVAL/zero and preserves the original chain. There
are no user pointers, allocation, raw addresses, arbitrary path names, recursive
formatting, or unbounded strings. Unknown calls also receive structured errors.
Exit and fatal CPU faults terminate the user context rather than return a syscall
failure; malformed getter calls deliberately do not recursively replace the chain.
Kernel-task and userspace error contexts are separate; new tasks/launches clear it.

## 4. Code and display

Working implementation: kernel/error.h, error.c, scheduler.c and syscall_entry.c.
User rendering is user/mini.c, which has bounded append and returns required text
length excluding NUL, truncating safely to capacity-1. For denied writes it renders:

```text
write failed, because permission denied, because the write capability is absent
```

The kernel cannot name an app-specific Documents alias it was never given; the
app can map its supplied directory handle to that label. Do not fabricate an
explanation about named resources when only an operation mask is known.

```c
struct syscall_result count = syscall_invoke(SYS_ERROR_GET, 0, 3, 0, 0, 0, 0);
/* Retrieve each cause with fields 0, 1, 2 before another ordinary syscall. */
```

## 5. Tests

`make test-extended` runs host layout/bounds/getter tests and real ring-3 denial,
getter and rendering checks. `make test` includes all regressions. The chain has
no untrusted byte parser; hostile indices are tested rather than dereferencing
random kernel pointers. The ELF/wire parsers are separately fuzzed.

## 6. Risks and limits

This is a small numeric cause chain, not arbitrary inter-service exception trees.
No asynchronous delivery or shared-thread last-error synchronization is provided.
Future threads need separate contexts. Last-error state is overwritten by ordinary
calls, so a UI must copy it before further work. Paths, credentials and kernel
addresses must not be added to details visible to unprivileged callers. Rendering
currently distinguishes authority errors and otherwise uses a generic rejection;
more domain-specific messages can be added without changing numeric ABI records.
