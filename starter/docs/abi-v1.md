# Current scalar syscall ABI: x86_64 v1.2

This is the current implementation contract. Earlier milestone notes describe
historical versions. The calling convention is shared between kernel test callers
and the real ring-3 runner, although extended resource calls require a live user
context. Only INT 0x80 is implemented, not the SYSCALL instruction.

RAX is the call number; arguments 0..5 are RDI, RSI, RDX, R10, R8, R9. Return RAX
is signed status (0 or negative error), RDX unsigned value (zero on failure).
Other GPRs and user RSP are preserved at the interrupt boundary. IRETQ restores
user flags, while assembly clears DF before C. User entry starts with IOPL=0.
The C wrapper follows ordinary SysV caller-saved rules while setting up arguments.
Unspecified argument words must be zero. Integers, sizes and handles are 64-bit;
boolean and type ranges are validated before narrowing. Pointers name user-arena
bytes only; no kernel pointers or native backend structures cross the boundary.

| No. | Name | Arguments | Success value |
| --- | --- | --- | --- |
| 0 | ABI_QUERY | required_major=1 | `(1 << 32) \| 2` |
| 1 | HANDLE_CLOSE | handle | 0 |
| 2 | HANDLE_DUPLICATE | source | same-rights handle; source requires DUPLICATE |
| 3 | HANDLE_RESTRICT | handle, subset_mask | 0; local irreversible restriction |
| 4 | HANDLE_CLONE | source, subset_mask | new handle; requires DUPLICATE |
| 5 | ERROR_GET | cause_index, field | cause field, or count for (0,3) |
| 6 | EXIT | code | no user return; low 8 bits become exit status |
| 7 | FS_OPEN | directory, name_ptr, name_len, rights, create_bool | file handle |
| 8 | FS_READ | file, output_ptr, length | byte count |
| 9 | FS_WRITE | file, input_ptr, length | byte count |
| 10 | FS_SEEK | file, absolute_offset | 0 |
| 11 | FS_UNLINK | directory, name_ptr, name_len | 0 |
| 12 | TX_BEGIN | namespace_root | initial value 0; after rollback value 1 |
| 13 | TX_COMMIT | none | 0 |
| 14 | TX_ROLLBACK | none | resumes successful BEGIN with value 1 |
| 15 | PIPE_CREATE | output_ptr_to_two_u64_handles | 0 |
| 16 | PIPE_SEND | write_handle, frame_ptr, frame_size | 0 |
| 17 | PIPE_RECEIVE | read_handle, output_ptr, capacity, expected_type | frame size |

The first pipe handle is READ, second WRITE; the 16-byte output contains little-
endian native u64 tokens. Creation validates/prepares that output before installing
handles. It does not implicitly transfer them to another user process.

## Status values

| Number | Symbol | Meaning |
| --- | --- | --- |
| 0 | OK | success |
| -1 | ENOSYS | unsupported call number |
| -2 | EINVAL | invalid/reserved argument or mask |
| -3 | EBADHANDLE | invalid/stale/wrong-type handle |
| -4 | ENOSPC | bounded file/table/queue buffer capacity exhausted |
| -5 | EOVERFLOW | object reference count exhausted |
| -6 | EABI | incompatible required major |
| -7 | EACCESS | required capability rights unavailable / attempted amplification |
| -8 | ENOENT | scoped name not found |
| -9 | EBUSY | open object, active/forbidden transaction operation |
| -10 | EFAULT | invalid/unwritable user buffer |
| -11 | EAGAIN | empty/full nonblocking pipe |
| -12 | EFORMAT | malformed typed frame or unexpected message type |
| -13 | ECONFLICT | transaction baseline revision changed |
| -14 | ENOMEM | COW output preparation cannot allocate a frame |

Loader/runner failures are trusted kernel API results rather than user syscalls:
bad ELF returns EFORMAT, mapping exhaustion ENOSPC, a user fault `-1000-vector`,
and the delivered-tick watchdog -2000. Unknown user call numbers always return
ENOSYS. Known calls reject reserved words before resource/pointer side effects.
On ordinary failure the current context receives a bounded structured chain; on
success it is cleared. ERROR_GET preserves it even on malformed getter queries.

Rights bits 0..5 are READ, WRITE, WAIT, CONTROL, DUPLICATE, TRANSFER. Unknown bits
are errors. File lookup/open combines directory READ with requested file rights
and WRITE for creation. Unlink needs directory WRITE; read/write need their
corresponding file rights. Seek affects only the held open description's cursor.
TX_BEGIN needs CONTROL on the exact namespace root supplied to this launch.
Error getter, ABI query and local pipe creation confer no external-resource
access. No global-root/console/device/network acquisition syscall exists.

Resource limits: 32 handles per context, eight 2048-byte RAM files and sixteen
open descriptions per namespace, four pipe objects with four 136-byte frames each,
one user runner, sixteen user page slots (14 loadable, one guard, one stack), one
transaction. Open/unlink/handle and pipe effects are forbidden during a transaction;
read/write/seek, queries, commit/rollback and terminating exit remain allowed.

There is no public process-creation syscall, kernel-pointer input, arbitrary
recipient selector, dynamic schema RPC, runtime exec or Linux-number compatibility.
Existing numbers/argument meanings must not be silently repurposed. Append new
calls and increment minor for compatible extensions; incompatible evolution needs
an explicitly designed major-version coexistence policy before a general release.
