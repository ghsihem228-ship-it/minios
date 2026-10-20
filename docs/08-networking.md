# v0.8: PCI, Ethernet and a bounded IPv4 host

MiniOS can now obtain a network configuration, exchange Ethernet/IP packets,
resolve a DNS name, and download a file to persistent storage. The end-to-end
example downloads `hello.elf` through QEMU's built-in TFTP server, saves it to
`/disk/net.elf`, then executes it as an isolated Ring 3 process.

## Device discovery and DMA

`pci.c` uses configuration mechanism 1 (`0xCF8/0xCFC`), scans buses/slots and
multifunction devices, and retains at most 64 records. `pci` shows IDs, classes
and BAR0; excess records are counted. The driver selects vendor/device
`10EC:8139`, requires an I/O BAR below 64 KiB, and enables I/O decoding and bus
mastering without writing PCI's write-one-to-clear status bits.

`rtl8139.c` resets the device with a bounded poll and disables NIC interrupts.
Its 8 KiB receive ring and four transmit buffers occupy static, aligned kernel
memory. The boot memory manager reserves these physical pages as part of the
kernel image; they remain supervisor-only in every process address space.
The kernel's low identity mapping supplies the DMA physical addresses. Receive
records are length/status checked, copied across ring wrap, and acknowledged
with CAPR. Transmit descriptors are reused only after hardware releases them;
short Ethernet frames are zero padded. Invalid receive records restart RX and
increment the driver error counter. This assumes coherent single-CPU x86 DMA;
no IOMMU, PCI bridges, MMIO NICs or hotplug support is implied.

## Scheduling and packet bounds

`net_poll()` processes at most eight received frames per call. It runs in task
0 while the shell waits for input and during network commands. Neither hardware
interrupt handlers nor Ring 3 syscalls parse packets. Network wait loops use
`task_idle()`, allowing the existing preemptive scheduler to run background user
processes. They accept Esc and have finite deadlines. The stack is deliberately
single-client: concurrent sockets and network syscalls need a later design.

Ethernet, IPv4 and UDP lengths are checked before any field/payload access.
IPv4 and ICMP checksums are mandatory; a nonzero UDP checksum is validated and
outgoing UDP has a checksum. Only our MAC/broadcast and our IP/broadcast are
accepted. IPv4 fragments are dropped; MTU is 1500, VLANs are unsupported. ICMP
echo requests addressed to us receive replies. `ping` sends three probes with
an ID, sequence and payload match and prints round-trip ticks (100 Hz).
Unknown/malformed/unmatched traffic increments `dropped`, which is not solely
an error counter. NIC ring errors have their own `driver-errors` counter.

ARP resolves a directly connected destination or the configured gateway, keeps
up to eight neighbors for 60 seconds, and retries three times. Replies must
match a pending lookup; incoming valid requests for our address are answered.
ARP and DHCP remain unauthenticated protocols; this is an educational stack,
not a hardened boundary for an untrusted LAN.

## DHCP and DNS

`dhcp` performs DISCOVER/OFFER/REQUEST/ACK with bounded retries, transaction and
MAC matching, option TLV checks, server selection and NAK handling. It requires
a contiguous subnet mask and a valid host address, checks the router's subnet,
and records a DNS server and lease. DHCP option overload is unsupported. Leases
expire locally; run `dhcp` again to reacquire rather than assuming renewal.
The common QEMU user-network defaults are:

| Item | IPv4 |
| --- | --- |
| Guest | 10.0.2.15 |
| Gateway and TFTP | 10.0.2.2 |
| DNS proxy | 10.0.2.3 |

`resolve <name>` queries A/IN over UDP using the DHCP-provided server. It checks
the response server/port, transaction, question and record boundaries. Compressed
names use bounded pointer traversal; CNAME following is capped at five passes.
Up to 16 answers/32 total records are accepted in a 512-byte DNS message. The
four-entry positive cache obeys the minimum CNAME/A TTL, capped at one hour.
Truncated responses return errno 75; there is no TCP fallback, EDNS, DNSSEC,
negative cache or AAAA lookup. DNS answers authenticate no remote code.

## TFTP and destination creation

`fetch <server-ip> <remote-name> <new-local-path>` uses an octet-mode TFTP RRQ,
512-byte DATA/ACK blocks and a fresh local UDP port. The transfer binds to the
server's transfer port after the first expected DATA block, re-ACKs duplicate
blocks and retransmits RRQ/ACK on timeout. Unexpected peers/blocks are dropped.
The maximum payload is 256 KiB, with three one-second waits per block and a
30-second overall bound. Zero-byte and exact-multiple-of-512 files work.
There is no option negotiation, upload, authentication/encryption or final-ACK
dally period. A dropped final ACK can leave the server retrying briefly after
the client has completed. Network timeouts and Esc leave no destination file.

The client buffers the bounded download in kernel heap, then opens a **new**
local file. `FS_EXCL=8` was added to the existing open flags (requires CREATE),
so a process creating the same path during a network wait cannot be truncated.
This also protects case-insensitive FAT aliases. Existing `cp` now uses the
same exclusive-create flag. No new syscall numbers are introduced.

Disk save errors are returned, and the client attempts to remove a failed new
file. A disk I/O failure may make FAT32 read-only and prevent cleanup; that
case retains the existing v0.7 failure semantics and may need offline repair.
Downloads do not add journaling or atomic crash-safe file replacement. RAMFS
still has a 4096-byte file cap; larger downloads belong on `/disk`. Transfer
heap memory is freed and unused heap pages are trimmed on every exit path.

Remote names are at most 63 characters, relative, and restricted to safe path
characters with no empty, dot or parent components. Destination paths retain
the VFS and FAT32 root/8.3 restrictions. TFTP is suitable for controlled local
experiments; downloaded ELF programs still run with the existing user-memory,
I/O and syscall restrictions.

## Running and validation

`make run` preserves/creates the FAT image and two host-side files under the
ignored `netfiles/` directory, then attaches `-device rtl8139` and QEMU's user
network/TFTP backend. Existing example files are never overwritten. Set
`NET_FILES` to expose a different host directory. No root/TAP setup is needed.
`make clean` removes neither host network files nor the disk image.

`make test-network` runs the shared pure wire decoders under ASan/UBSan, including
truncation/compression/TLV tests and 100,000 deterministic fuzz cases. It also
boots the actual kernel under QEMU TCG:

- The real SLIRP backend supplies DHCP, ARP, ICMP and TFTP at 2/64 MiB. Downloaded
  ELF bytes are inspected independently on FAT32 and executed in Ring 3.
- A local framed-Ethernet peer checks outgoing checksums and supplies compressed
  DNS CNAME/A, cached results, NXDOMAIN and silence. It injects bad checksums,
  fragments, malformed DHCP/DNS, wrong transfer IDs, dropped RRQ and duplicate
  DATA. Larger transfers wrap the receive ring repeatedly.
- Tests check empty/exact-block files, oversize/RAMFS limits, timeouts, Esc,
  exclusive creation, link-down, missing hardware, heap/frame recovery and
  continued background process scheduling. A fresh offline VM verifies disk
  persistence; the independent host FAT reader checks contents/chains/mirrors.

No public Internet service is needed by these tests. External DNS and reachable
remote servers depend on the host's networking. Physical RTL8139 hardware has
not been validated. `make test` also runs the complete existing 2/64/128 MiB
memory/process and storage/fault-injection regressions.
