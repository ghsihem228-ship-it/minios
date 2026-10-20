# v0.9: user-space UDP/TCP and HTTP

The network stack now serves Ring 3 programs. A process can create a socket,
start a TCP connection, queue a bounded send, read a byte stream/datagram,
inspect readiness, and close its own descriptor. `/bin/http` demonstrates the
whole path by requesting a file from an actual host HTTP server through QEMU's
SLIRP backend. The v0.8 shell networking commands remain available.

## Ownership, memory and scheduling

`socket.c` owns eight static records, each with a 1024-byte transmit buffer and
2048-byte receive buffer. Each user process has at most four socket descriptors
(numbered 0–3). These form a separate namespace from file descriptors; every
lookup includes the current PID, so knowing another process's descriptor number
does not confer access. A closing detached TCP record still consumes a global
slot. Exhaustion returns errno 24 rather than allocating unbounded kernel memory.
The RAMFS node limit increased to 24 to accommodate the new embedded programs.

Socket syscalls run with IF clear and perform bounded validation/copy/state
changes. They never wait, poll the NIC, retain a user pointer, or switch to
another process midway through a call. Existing page-table checks validate
whole user ranges, including write permissions for receive destinations. Each
send/receive call has a 1024-byte limit. Zero-length calls do not dereference
user memory.

`net_poll()` invokes the socket receive and service paths in task 0, with at
most eight Ethernet frames per poll and eight records to service. TCP and UDP
output use a nonblocking ARP lookup; unresolved neighbors cause a retry rather
than sleeping inside the service loop. `task_wait()` now polls networking while
waiting for a foreground program, as the shell input loop already did. User
programs call sleep/yield when they receive EAGAIN or EINPROGRESS; timer-based
preemption still governs CPU-bound programs.

A process exit, fault or kill enters the existing zombie/reaping path. Reaping
releases UDP records and schedules a best-effort TCP reset before freeing active
connections. No user buffers survive the syscall that submitted them. An
explicit TCP close releases the descriptor immediately and lets task 0 finish
queued data and FIN/ACK closure with a deadline. Detached records are freed on
closure, timeout or network failure. A DHCP reconfiguration invalidates existing
connections; there is no transparent reconnection.

## System call ABI

Use `int 0x80`; EAX is the syscall number/result. Results are nonnegative values
or negative errno. Existing calls 1–14 are unchanged. IPv4 addresses and ports
are numeric **host-order integers**: `0x0A000202` means `10.0.2.2`, and `8080`
means port 8080. Callers do not pass a `sockaddr` structure.

| EAX | Operation | EBX | ECX | EDX | Result |
| --- | --- | --- | --- | --- | --- |
| 15 | socket | type: 1 UDP / 2 TCP | — | — | local socket descriptor |
| 16 | connect | socket | IPv4 | port | UDP: 0; TCP: -115 while connecting |
| 17 | send | socket | readable user buffer | length | bytes copied into send queue |
| 18 | recv | socket | writable user buffer | capacity | bytes delivered, 0 for TCP EOF |
| 19 | socket close | socket | — | — | 0 |
| 20 | socket status | socket | — | — | readiness flags or stored error |
| 21 | UDP bind | socket | port | — | 0 |

Status flags: CONNECTED=1, READABLE=2, WRITABLE=4, EOF=8, CONNECTING=16.
An unconnected newly created socket has status 0. A stored error is returned by
status/send; recv drains buffered TCP bytes before returning a stored error.
WRITABLE means the local transmit queue can accept data, not that the peer has
acknowledged it. Reads on an empty queue return EAGAIN 11 unless TCP has reached
EOF. A zero-capacity receive returns 0 without consuming buffered data.

TCP connect is asynchronous; poll status until CONNECTED or an error, sleeping
or yielding between attempts. Calling connect again while connecting returns
EALREADY 114; other reuse returns EISCONN 106. A queued send may fail later:
check status/receive rather than assuming that a positive send result confirms
remote delivery. Only one send is queued per socket; another returns EAGAIN.

Common errors: EBADF 9, E2BIG 7, EFAULT 14, EINVAL 22, EMFILE 24,
EADDRINUSE 98, ENETDOWN 100, ENETRESET 102, ECONNRESET 104, ENOTCONN 107,
ETIMEDOUT 110, ECONNREFUSED 111, EALREADY 114, EINPROGRESS 115.

UDP connect selects the one allowed remote IP/port, performs no handshake and
enables send/receive filtering. Bind is optional, UDP-only, before connect, and
limited to local ports 1024–49151. Automatic socket ports use 32768–49151;
v0.8 shell DNS/TFTP uses 49152–65535. The socket allocator does not reuse an
active local port. Two received datagrams of up to 1024 bytes fit per UDP
socket; extra/oversize datagrams are dropped. A short receive buffer truncates
the datagram and consumes the remainder. A valid zero-byte datagram returns 0;
use READABLE to distinguish queued UDP data. There is no recvfrom/sendto,
unconnected receive, broadcast, multicast or automatic UDP retry.

## TCP state and wire checks

`tcp_wire.c` validates the checksum, length/data offset, ports and bounded option
TLVs before exposing a payload. The SYN MSS option controls segment size, capped
at 512 bytes. Urgent data and ECN flags are unsupported and rejected. Window
scaling, timestamps and SACK are not negotiated. Bad checksums, invalid options,
contradictory control flags and unrecognized tuples are rejected.

The client supports active three-way handshakes, established streams,
CLOSE_WAIT, FIN_WAIT_1/2, CLOSING, LAST_ACK and a bounded TIME_WAIT. There is one
unacknowledged segment per socket. Partial acknowledgements advance the queued
buffer and sequence, so retransmission sends only the remaining suffix. ACKs
beyond transmitted bytes are rejected. Sequence comparisons handle 32-bit wrap.
Incoming data must meet the receive edge; duplicate prefixes are trimmed,
future segments are discarded and acknowledged at the current edge, and new
bytes after EOF are refused. FIN consumes one sequence number once; duplicate
FINs are acknowledged without duplicating EOF/data. Valid resets must match the
receive edge (or acknowledge our SYN during connection establishment).

The advertised receive window is the free space in a 2048-byte circular stream
buffer. A user read queues a window update. Sending respects the peer window;
a closed window triggers bounded one-second probes, allowing a lost window
update to recover. Retransmission uses a fixed one-second timer, at most four
transmissions per outstanding segment, and six-second no-progress deadlines
for connecting, queued data and closing. Each valid forward ACK refreshes the
progress deadline. Idle established connections remain open until the process
closes/exits or the network fails. TIME_WAIT lasts two seconds and duplicate
FINs refresh it; this is a small local-lab bound, not a full Internet 2MSL policy.

This is an educational stop-and-wait TCP client. It has no listen/accept,
passive opens, out-of-order queue, congestion-control algorithm, exponential
RTO/backoff, TCP Fast Open, keepalive, TLS, IPv6, POSIX compatibility or firewall.
Packets for unknown TCP tuples are dropped. Resource limits and deadlines are
part of the ABI; this milestone does not claim production TCP compliance.

## Programs and interactive workflow

Build and run the suite before starting a manual server:

```bash
make
make test-sockets
```

In a second Ubuntu/WSL terminal, from the checkout:

```bash
make netfiles
python3 -m http.server 8080 --bind 127.0.0.1 --directory netfiles
```

Start `make run` in the first terminal. Inside MiniOS:

```text
dhcp
run /bin/http
sockets
```

`/bin/http` uses the fixed host endpoint `10.0.2.2:8080`, sends HTTP/1.0
`GET /welcome.txt`, prints all response bytes, then prints the byte count and
FNV-1a digest in hexadecimal. It drains until EOF and closes the socket. Its
application wait deadline is ten seconds. It does not parse HTTP status,
Content-Length, redirects, chunking or TLS; protocol bytes are shown directly.
The host's directory is served by Python, independently of the TFTP backend.
Stop the server with Ctrl+C when done; a manual server on port 8080 conflicts
with the test suite's listener, so stop it before rerunning tests.

`/bin/udpecho` expects one exact UDP echo of `Hello from Ring 3 UDP!` at the
host's port 9000. The automated suite supplies that service. `/bin/sockprobe`
checks invalid types/descriptors, user permissions, range limits and socket
quota/reaping. `/bin/sockhold` opens a TCP connection and sleeps, for
`sockets`/`ps`/`kill` cleanup experiments. None requires a public Internet service.

## Validation

`make test-sockets` runs both host packet sanitizer harnesses and actual QEMU
TCG guests. The TCP harness checks short/corrupt headers and malformed options,
and fuzzes 100,000 deterministic inputs with ASan/UBSan. The earlier DNS/DHCP
harness contributes another 100,000 cases.

Actual SLIRP interoperability uses the host's TCP/UDP sockets at 2/64 MiB.
The HTTP response exceeds the guest stream buffer, and its exact length/FNV
hash is checked against independently generated bytes. Tests also check socket
ownership while another PID holds descriptor 0, four-descriptor quota,
user-pointer permissions, link-down, kill/exit cleanup, and frame recovery.

An independent Ethernet/TCP peer verifies outgoing checksums and injects SYN
loss, data loss, partial ACKs, duplicate DATA/FIN, a lost FIN ACK, future data,
invalid ACKs, invalid reset sequences and bad checksums. Its initial sequence
crosses 32-bit wrap. Separate cases cover zero-window recovery, refusal,
silence/timeouts and a valid reset. The peer sends UDP traffic from a wrong port before the valid echo, verifying
that the kernel ignores it. Connection records must drain
back to zero after each test.

`make test` additionally runs the complete existing memory/process, storage,
and v0.8 DHCP/DNS/TFTP integration suites. Temporary listeners bind only loopback
ports 8080/9000, and tests shut down their own servers/VMs. Physical NICs and
public Internet servers are not part of this validation.
