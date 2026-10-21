# Step 13: bounded typed message pipes

## 1. Alternatives

Byte streams compose with Unix tools but require each consumer to frame and parse
its input. Fully reflected schemas/recursive objects permit rich data but bring
versioning, depth limits and pointer/ownership complexity. A small canonical TLV
format gives explicit types and message boundaries with a bounded validator.

## 2. Recommendation

Use little-endian, pointer-free frames with scalar types and flat records. Keep
messages atomic: invalid type/capacity does not consume a queued message. Expose
separate READ and WRITE handles, derived by ordinary capability rules. This is a
new native message API; the POSIX subset does not misrepresent it as byte pipe().

## 3. Wire and syscall specification

```text
Outer header (8 bytes): u16 version=1, u16 type, u32 payload_length
Payload: exactly payload_length bytes; at most 128
Types: 1 unsigned u64 (8 bytes), 2 signed two's-complement i64 (8 bytes),
       3 bool (one byte 0 or 1), 4 UTF-8, 5 bytes, 6 flat record
Record field: u16 nonzero field_id, u16 scalar_type, u32 field_length, payload
```

Field IDs must be strictly increasing; at most eight fields; nested records are
rejected. Empty text/bytes/records are valid. UTF-8 rejects overlong encodings,
stray/truncated continuations, surrogates and values above U+10FFFF. Numeric types
are fixed-width little endian. There are no transmitted pointers, native structs,
handles, floats, unbounded lists or implicit NUL terminators. Sender and receiver
agree application meanings for field IDs; type validation is not schema negotiation.

```text
15 PIPE_CREATE(user_pointer_to_two_u64_handles) -> 0
16 PIPE_SEND(write_handle, frame_pointer, frame_length) -> 0
17 PIPE_RECEIVE(read_handle, buffer, capacity, expected_type) -> frame_length
```

Other args are zero. Four pipe objects each hold four messages of at most 136 bytes
including header. Creation gives READ|DUPLICATE|TRANSFER and WRITE|DUPLICATE|TRANSFER
handles on the same object; failure installing the second handle closes the first
and frees the object. Object allocation is local bounded creation, not authority
to any external resource. Send requires WRITE, receive READ. Copyin occurs before
validation/enqueue; complete output-range checks/COW preparation precede dequeue.

Full and empty queues return EAGAIN, type mismatch EFORMAT, small buffers ENOSPC.
Failed receive leaves the message queued; successful receive returns its complete
frame. The caller must request an exact outer type (no wildcard). Last object
reference frees queue storage for reuse. There is no blocking wait or EOF/peer-
closed notification yet; closing one side can leave EAGAIN polling on the other.

## 4. Working code

kernel/pipe.c implements the pure validator, object pool and atomic queue operations;
services.c supplies safe user-buffer handling. The ring-3 demo sends and receives
an actual flat record with a u64 field and boolean field, checking a wrong expected
type first to verify non-consumption. The public constants are in pipe.h and the
scalar syscall ABI header.

```text
Example bool true frame: 01 00 03 00 01 00 00 00 01
```

## 5. Tests

`make test-extended` runs real kernel/host queue tests for rights, capacity, full/
empty behavior, round-trip bytes and type mismatch. Host validation fuzzes 100,000
bounded random frames under ASan/UBSan and explicitly rejects overlong UTF-8.
QEMU executes the record test in ring 3 and a separate producer/consumer
kernel-task test with explicitly granted READ-only and WRITE-only handles. Future coverage-guided fuzzing, schema
compatibility tests and multi-process transfer tests remain useful follow-up work.

## 6. Risks and limits

Framing is not authorization, semantic schema validation, flow-control scheduling,
remote authentication or a safe filesystem RPC service. No blocking, cancellation,
EOF signaling, descriptor passing or multi-process IPC transport is implemented.
A receiver can keep rejecting an unexpected message and block progress until it
chooses a compatible type or closes the pipe. A future protocol needs explicit
schema/version negotiation and application-level rejection policy. Limits prevent
memory exhaustion per object; global pipe/handle quotas still bound total use.

## Subsequent implementation

Milestone 16 supersedes the original endpoint lifetime: pipes now have separate read/write objects, EOF, peer-closed signals and blocking readiness waits.
