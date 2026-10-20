# Step 6 only: a scalar syscall ABI and tested entry path

## 1. Alternatives and trade-offs

| Decision | Alternative A | Alternative B | Recommendation for this milestone |
| --- | --- | --- | --- |
| Entry | INT 0x80 through a normalized IDT frame | SYSCALL via EFER/STAR/LSTAR/FMASK and explicit stack switching | INT: reuse the verified save/restore and IRETQ path first |
| Return | Negative result encoded in one register, positive values on success | Separate status/value registers | RAX=status and RDX=value, preserving all 64 handle bits |
| Parameters | Pass a pointer to a versioned request block | Six scalar registers | Scalars now: no copying, pointer faults, padding or TOCTOU surface |
| ABI evolution | Renumber/repurpose calls as kernel grows | Explicit version query, append calls, preserve existing meanings | Stable numeric v1 scalar contracts; no public kernel layouts |

SYSCALL is the eventual fast-path candidate, but it does not automatically switch
RSP to a kernel stack. It requires deliberate stack/TLS handling, entry flag
masking, and safe return-address/flag validation, especially for SYSRET. INT uses
more machinery but gives an architectural frame and avoids inventing that path
before user processes exist. SYSENTER is not needed for this x86_64-only starter.

A pointer request structure can scale to many fields, but cannot safely ship
without a defined user address-space/copyin/copyout contract. A single signed
return register would make handles with their high bit set ambiguous with errors.
Separate status/value avoids that problem without an output pointer.

## 2. Recommendation and precise scope

Implement three calls through the existing vector-128 assembly stub and C
interrupt dispatcher. Keep **the IDT gate DPL0**. Ring-3 programs, TSS.RSP0 setup,
user mappings, an ELF loader and recoverable user-copy faults are absent in this
starter. Raising DPL now would expose an incomplete entry boundary. Step 6 is a
working kernel-invoked syscall mechanism and scalar dispatch layer, **not a
working ring-3 syscall service**. The staged limitation is intentional.

The future userspace milestone must implement and test the privilege transition
before enabling DPL3. Nothing here claims to validate user pointers or isolate
malicious callers. Capability enforcement is step 7; structured cause-chain errors
are step 8. Neither is silently implemented early.

## 3. Concrete ABI and invariants

`kernel/syscall_abi.h` contains explicitly sized types, numeric constants and no
kernel pointers or internal object layouts. The v1 scalar register contract is:

| Register | Meaning |
| --- | --- |
| RAX on entry | Unsigned 64-bit call number |
| RDI, RSI, RDX, R10, R8, R9 | Six unsigned 64-bit argument words |
| RAX on return | Signed 64-bit status: 0 success, negative failure |
| RDX on return | Unsigned 64-bit value; always zero on failure |
| Other GPRs, RSP | Preserved by architectural entry/return |
| Saved RFLAGS | Restored by IRETQ; DF is cleared while C executes |

The C wrapper uses ordinary SysV caller-saved register rules; its own setup may
clobber those registers. The preservation guarantee describes the INT instruction
boundary, not the surrounding C function call. No SIMD/x87/TLS state is supported.

```c
struct syscall_result { int64_t status; uint64_t value; };
struct syscall_result syscall_invoke(uint64_t number, uint64_t a0, uint64_t a1,
                                    uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
```

The two-member structure is returned in RAX/RDX by the x86_64 SysV C ABI. It is not
a user buffer and no pointer to it crosses the interrupt boundary. The assembly
wrapper remaps normal C arguments (including the seventh stack argument) to the
register convention above. Layout assertions check the 16-byte result structure.

| Number | Name | Argument 0 | Success value |
| --- | --- | --- | --- |
| 0 | SYS_ABI_QUERY | Required major, currently 1 | `(major << 32) \| minor`, currently `1 << 32` |
| 1 | SYS_HANDLE_CLOSE | Local handle | 0 |
| 2 | SYS_HANDLE_DUPLICATE | Local source handle | New local handle |

Every implemented call requires arguments 1..5 to be zero. Unknown numbers return
SYS_ENOSYS before examining arguments. Known numbers with nonzero reserved
arguments return SYS_EINVAL before any side effect. A version query requiring
another major returns SYS_EABI. Unsupported future calls remain unsupported;
there is no placeholder implementation of an imagined 50–100-call API.

| Status | Number | Meaning |
| --- | --- | --- |
| SYS_OK | 0 | Success |
| SYS_ENOSYS | -1 | Unknown call number |
| SYS_EINVAL | -2 | Nonzero reserved arguments |
| SYS_EBADHANDLE | -3 | Invalid/stale local handle |
| SYS_ENOSPC | -4 | No reusable handle slot |
| SYS_EOVERFLOW | -5 | Object reference count exhausted |
| SYS_EABI | -6 | Requested ABI major unsupported |

These v1 numbers and meanings are explicit contracts, independent of private
enum object_status values. New calls receive new numbers; existing numbers must
not be repurposed. Incompatible major-version coexistence and ring-3 return policy
must be specified before a public userspace release. This development kernel is
not claiming that its entire future public ABI is already frozen.

Internal implementation interfaces:

```c
struct syscall_result syscall_execute(struct handle_table *table,
                                      uint64_t number, const uint64_t args[6]);
void syscall_dispatch(struct interrupt_frame *frame);
int syscall_selftest(void);
```

Invariants:

1. Vector 128 remains a present DPL0 interrupt gate with no IST. Hardware clears
   IF, existing assembly saves all GPRs, normalizes vector/error, clears DF and
   aligns the C stack. C overwrites only saved RAX/RDX; normal epilogue IRETQs.
   Other exception/IRQ paths retain their existing behavior.
2. Callers are trusted ring-0 task contexts after scheduler initialization, never
   IRQ/NMI handlers. Current dispatch does not block, sleep, yield or enable IRQs.
   Object destructors invoked by close must obey the same no-yield contract.
3. The entry adapter derives the table from trusted current-task state. No syscall
   argument chooses another task or supplies a kernel table/object pointer.
4. Raw scalar arguments are never interpreted as addresses. A malformed handle
   only reaches the bounded generation-checked table lookup. Reserved argument
   rejection precedes close/duplicate so a failed call has no table side effect.
5. Close and duplicate preserve step 5's reference ownership and stale-token
   rules. Duplicate does not transfer ownership to another task, grant rights,
   create an object, or add a namespace. No ambient file/device access is exposed.
6. Inputs to the internal C dispatcher are trusted kernel pointers. User-memory
   validation is not achieved by checking only canonical addresses or leaf PTEs.
   Future copy helpers must check bounds/overflow, effective permissions at every
   page-table level, and fault handling/lifetimes across concurrent changes.
7. Results contain scalar constants or handles, never kernel addresses. No error
   message string is emitted from the normal handler and no dynamic allocation
   is required. Close may destroy an object; destructor cost is object-dependent.

## 4. Working code, structure and build

```text
kernel/syscall_abi.h       scalar v1 constants and C wrapper contract
kernel/syscall.h           internal dispatch interfaces
kernel/syscall.c           portable scalar validation and execution
kernel/syscall_entry.c     interrupt-frame adapter and live boot checks
boot/syscall.asm           C argument adapter, INT80, register/flag probe
kernel/interrupts.c        vector-128 dispatch using existing DPL0 gate
kernel/main.c              live self-test and success marker
tests/test_syscall.c      host policy tests and scalar fuzzing
```

The files contain the complete commented C/assembly. A real example, also exercised
by the compiled kernel self-test:

```c
struct syscall_result r = syscall_invoke(SYS_ABI_QUERY, 1, 0, 0, 0, 0, 0);
if (r.status == SYS_OK && r.value == SYS_ABI_VERSION) {
    /* The supported major/minor is 1.0. */
}
```

Run inside `starter/`:

```bash
make
make test-syscalls
make test
make run
```

For this prepared cloud host, select its installed tools first:

```bash
export PATH=/workspace/.minios-tools/bin:$PATH
```

Normal boot prints this after the handle self-test and before READY:

```text
PASS syscalls: INT80, registers, ABI, handles, rejection
READY: interrupts enabled; idle
```

## 5. Tests and fuzzing

QEMU runs real INT80/IRETQ, negotiates v1, rejects an unsupported major and the
sixth nonzero argument (testing the C stack-to-register wrapper), duplicates and
closes handles, rejects stale close, and checks final destruction. A dedicated
assembly probe fills every input GPR with full-width sentinels and sets DF. An
unknown call must return ENOSYS/zero, preserve the other thirteen GPRs and return
with the original DF/IF. The C wrapper restores normal ABI DF=0 before returning.

`make test-syscalls` runs this integration at 16/64/128 MiB and a host harness using
the actual syscall/handle/object code under ASan/UBSan. Host tests enumerate each
reserved argument for all three known calls and require no side effect; they
check capacity exhaustion, refcount overflow, stale handles and cleanup. Another
100,000 deterministic scalar cases exercise arbitrary numbers/arguments against
an independent rejection oracle. Random values are not treated as valid pointers.
This is one reproducible seed, not coverage-guided fuzzing or a security proof.

`make test` reruns protected-memory, exception, IRQ, IDT/TSS/IST, CPU-rejection,
module-reservation and existing host model/fuzz tests. Existing table inspection
requires the actual vector-128 gate to remain DPL0. No CPL3 entry/return test or
untrusted user-pointer test is claimed: those facilities do not yet exist.

## 6. Risks and common mistakes

Do not enable DPL3 by changing one IDT byte. Missing RSP0, user segment/mapping
support and safe return policy can turn the first ring-3 interrupt into a double
or triple fault. Similarly, SYSCALL must not execute C on an untrusted user stack.
The current kernel-only test path cannot establish security against user programs.

Never select a handle table using a caller-supplied identifier. Never reinterpret
a handle or scalar word as a pointer. Do not encode success handles in the signed
status register, truncate the handle to 32 bits, mutate the saved argument RDX
before copying inputs, or call the cooperative scheduler from this ISR context.

Current calls run with interrupts disabled. Table operations are bounded, but
future blocking/destructive implementations could add interrupt latency or
deadlocks; redesign the execution context before adding such calls. Do not
expose logging, paths, clock access or object creation as shortcuts around the
future capability design. Numeric statuses are deliberately not structured
cause chains yet, and private C enum values must not leak into the public ABI.

Stop at step 6. Wait for `next` before capability-system work.
